/************************************************************************
| $Id: cl_thief.cpp,v 1.45 2004/04/23 21:13:35 urizen Exp $
| cl_thief.C
| Functions declared primarily for the thief class; some may be used in
|   other classes, but they are mainly thief-oriented.
*/
#include <character.h>
#include <structs.h>
#include <utility.h>
#include <spells.h>
#include <levels.h>
#include <player.h>
#include <obj.h>
#include <room.h>
#include <handler.h>
#include <mobile.h>
#include <fight.h>
#include <connect.h>
#include <interp.h>
#include <act.h>
#include <db.h>
#include <string.h>
#include <returnvals.h>

extern int rev_dir[];
extern CWorld world;
 
extern struct str_app_type str_app[];
extern struct index_data *mob_index;
extern struct index_data *obj_index;

int find_door(CHAR_DATA *ch, char *type, char *dir);
struct obj_data * search_char_for_item(char_data * ch, sh_int item_number);

int palm(CHAR_DATA *ch, struct obj_data *obj_object,
          struct obj_data *sub_object)
{
  char buffer[MAX_STRING_LENGTH];
  bool successful = 1;
  byte percent;
  int learned, specialization, chance;
  
  percent = number(1,101);

  learned = has_skill(ch, SKILL_PALM);
  specialization = learned / 100;
  learned = learned % 100;

  chance = 50;
  chance += GET_DEX(ch);
  chance += learned / 10;

  if(percent > chance)
    successful = 0;  // if percent is too high, thief will fail the attempt
  

  if(IS_SET(obj_object->obj_flags.more_flags, ITEM_UNIQUE)) {
    if(search_char_for_item(ch, obj_object->item_number)) {
       send_to_char("The item's uniqueness prevents it!\r\n", ch);
       return eFAILURE;
    }
  }

  skill_increase_check(ch, SKILL_PALM, learned, SKILL_INCREASE_EASY);

  move_obj(obj_object, ch);
  
  if(successful) {
    act("You successfully snag $p, no one saw you do it!",  ch, 
          obj_object, 0, TO_CHAR, 0);
    act("$n palms $p trying to hide it from your all knowing gaze.",
          ch, obj_object, 0, TO_ROOM, GODS);
  }
  else {
    act("You clumsily take $p...", ch, obj_object, 0, TO_CHAR, 0);
    if(sub_object) act("$n gets $p from $P.", ch, obj_object, sub_object,
           TO_ROOM, INVIS_NULL);
    else
      act("$n gets $p.", ch, obj_object, 0, TO_ROOM, INVIS_NULL); 
  }
  if((obj_object->obj_flags.type_flag == ITEM_MONEY) &&
     (obj_object->obj_flags.value[0] >= 1)) {
    obj_from_char(obj_object);
    sprintf(buffer, "There was %d coins.\n\r",
      obj_object->obj_flags.value[0]);
    send_to_char(buffer, ch);
    GET_GOLD(ch) += obj_object->obj_flags.value[0];
    extract_obj(obj_object);
  }
  return eSUCCESS;
}

int do_eyegouge(CHAR_DATA *ch, char *argument, int cmd)
{
  CHAR_DATA *victim;
  char name[256];
  int learned = has_skill(ch, SKILL_EYEGOUGE);
  argument = one_argument(argument,name);

  if(!(victim = get_char_room_vis(ch, name)) && (victim = ch->fighting)==NULL) {
    send_to_char("There's noone like that here to eyegouge.\n\r", ch);
	    return eFAILURE;
  }
  if (IS_AFFECTED(victim, AFF_BLIND))
  {
    send_to_char("They're already blinded!\r\n",ch);
    return eFAILURE;
  }
  if(victim == ch) {
    send_to_char("That sounds... stupid.\n\r", ch);
    return eFAILURE;
  }
 if(!can_be_attacked(ch, victim) || !can_attack(ch))
    return eFAILURE;

  if (!learned)
  {
    send_to_char("You would.. if you knew how.\r\n",ch);
    return eFAILURE;
  }
  int retval = 0;
  if (learned < number(1,101))
  {
     act("You miss $N's eye.",ch, NULL, victim, TO_CHAR, 0);
     act("$n walks up to $N and tries to push $s thumb into $N's eye, but misses!",ch, NULL, victim, TO_ROOM, NOTVICT);
     act("$n walks up to you and tries to push $s thumb into your eye, but misses!",ch, NULL, victim, TO_VICT, 0);
     retval = damage(ch,victim, 0, TYPE_UNDEFINED, SKILL_EYEGOUGE, 0);
  } else {
     act("You press your thumb into $N's eye.",ch, NULL, victim, TO_CHAR, 0);
     act("$n walks up to $N and pushes $s thumb into $N's eye!",ch, NULL, victim, TO_ROOM, NOTVICT);
     act("$n walks up to you and pushes $s thumb into your eye! Ow!",ch, NULL, victim, TO_VICT, 0);
     int dam = 100 * learned>50?learned:50;
     retval = damage(ch, victim, dam, TYPE_UNDEFINED, SKILL_EYEGOUGE, 0);
     SET_BIT(victim->affected_by, AFF_BLIND);
     SET_BIT(victim->combat, COMBAT_THI_EYEGOUGE);
  }
  WAIT_STATE(ch, PULSE_VIOLENCE);
   skill_increase_check(ch, SKILL_EYEGOUGE, learned, SKILL_INCREASE_MEDIUM);
  return retval | eSUCCESS;
}

int do_backstab(CHAR_DATA *ch, char *argument, int cmd)
{
  CHAR_DATA *victim;
  char name[256];
  int percent, specialization, chance;
  int skill = 0;
  int was_in = 0;
  int retval;
  int learned;

  one_argument(argument, name);

  if(!(victim = get_char_room_vis(ch, name))) {
    send_to_char("Backstab whom?\n\r", ch);
    return eFAILURE;
  }

  if(victim == ch) {
    send_to_char("How can you sneak up on yourself?\n\r", ch);
    return eFAILURE;
  }

  if(IS_MOB(victim) && IS_SET(victim->mobdata->actflags, ACT_HUGE)) {
    send_to_char("You can't backstab someone that HUGE!\r\n", ch);
    return eFAILURE;
  }

  if(IS_AFFECTED2(victim, AFF_ALERT)) {
    send_to_char("They're too alert and nervous looking around.  You can't sneak behind!\r\n", ch);
    return eFAILURE;
  }

  int min_hp = (int) (GET_MAX_HIT(ch) / 5);
  min_hp = MIN( min_hp, 25 );

  if( GET_HIT(ch) < min_hp ) {
    send_to_char("You are feeling too weak right now to attempt such a bold maneuver.\r\n", ch);
    return eFAILURE;
  }

  // Check the killer/victim
  if((GET_LEVEL(ch) < G_POWER) || IS_NPC(ch)) {
      if(!can_attack(ch) || !can_be_attacked(ch, victim))
      return eFAILURE;
  }

  if(!ch->equipment[WIELD]) {
    send_to_char("You need to wield a weapon to make it a success.\n\r", ch);
    return eFAILURE;
  }

  if(ch->equipment[WIELD]->obj_flags.value[3] != 11 && ch->equipment[WIELD]->obj_flags.value[3] != 9) {
    send_to_char("You can't stab without a stabby weapon...\n\r", ch);
    return eFAILURE;
  }

  if(victim->fighting) {
    send_to_char("You can't backstab a fighting person, too alert!\n\r", ch);
    return eFAILURE;
  }
  
  percent = number(1, 101); // 101% is a complete failure

  WAIT_STATE(ch, PULSE_VIOLENCE);
  
  int itemp = number(1, MAX_MORTAL);

  if(IS_NPC(ch) || GET_LEVEL(ch) > ARCHANGEL)
    skill = 75;
  else skill = has_skill(ch, SKILL_BACKSTAB);

  specialization = skill / 100;
  skill = skill % 100;

  if(skill) 
  {
    chance = 70;
    chance += skill / 10;
    chance += (GET_DEX(ch) - GET_DEX(victim)) / 2;
    
    skill_increase_check(ch, SKILL_BACKSTAB, skill, SKILL_INCREASE_MEDIUM);
  }
  else {
    send_to_char("You don't know how to backstab people!\r\n", ch);
    return eFAILURE;
  }

  // record the room I'm in.  Used to make sure a dual can go off.
  was_in = ch->in_room;

  // if they're hurt they are going to be suspicious of backstabs so half the chance
  if( ( (float)GET_HIT(victim) / (float)GET_MAX_HIT(victim) ) < .7) 
  {
    chance /= 2;
    chance += 5;
  }

  // failure
  if(AWAKE(victim) && (percent > chance))
    retval = damage(ch, victim, 0, TYPE_UNDEFINED, SKILL_BACKSTAB, 0);

  // success
  else if(
          ( ( GET_LEVEL(victim) < IMMORTAL && !IS_NPC(victim) ) 
            || IS_NPC(victim)
          ) 
          && (GET_LEVEL(victim) <= GET_LEVEL(ch) + 5) 
          && ((!IS_NPC(ch) && GET_LEVEL(ch) >= IMMORTAL) || itemp > 46 || 
               ( !IS_NPC(victim) && IS_SET(victim->pcdata->punish, PUNISH_UNLUCKY) )
             )
         ) 
  { 
    act("$N crumples to the ground, $S body still quivering from\n\r"
        "$n's brutal assassination.", ch, 0, victim, TO_ROOM, NOTVICT);
    act("You feel $n's blade slip into your heart, and all goes black.",
        ch, 0, victim, TO_VICT, 0);
    act("BINGO! You brutally assassinate $N, and $S body crumples\n\r"
        "before you.", ch, 0, victim, TO_CHAR, 0);
    return damage(ch, victim, 9999999, TYPE_UNDEFINED, TYPE_UNDEFINED, 0); 
  }
  else
    retval = attack(ch, victim, SKILL_BACKSTAB, FIRST);

  if(!IS_SET(retval, eCH_DIED))
    WAIT_STATE(ch, PULSE_VIOLENCE);

  if(SOMEONE_DIED(retval))
    return retval;

  // dual backstab
  if((GET_LEVEL(ch) >= 40)                                            &&
     (was_in == ch->in_room)                                          && 
     ((GET_CLASS(ch) == CLASS_THIEF) || (GET_LEVEL(ch) >= ARCHANGEL)) &&
     (ch->equipment[SECOND_WIELD])                                    &&
     ((ch->equipment[SECOND_WIELD]->obj_flags.value[3] == 11) ||
       (ch->equipment[SECOND_WIELD]->obj_flags.value[3] == 8))        &&
     (learned = has_skill(ch, SKILL_DUAL_BACKSTAB))
    )
  {
        skill_increase_check(ch, SKILL_DUAL_BACKSTAB, skill, SKILL_INCREASE_HARD);

        if(number(1, 100) <= learned)
        {
           WAIT_STATE(ch, PULSE_VIOLENCE);
           percent = number(1, 101);
           if (AWAKE(victim) &&
              (percent > skill))
              return damage(ch, victim, 0, TYPE_UNDEFINED, SKILL_BACKSTAB, SECOND);
           else
              return attack(ch, victim, SKILL_BACKSTAB, SECOND);
        }
  }
  return eSUCCESS;
}

int do_circle(CHAR_DATA *ch, char *argument, int cmd)
{
   CHAR_DATA * victim;
   // char name[256];
   byte percent;
   int learned, specialization, chance;
   int retval;

   if(IS_MOB(ch))
     learned = 75;
   else if(!(learned = has_skill(ch, SKILL_CIRCLE))) {
     send_to_char("You dunno how to circle!\r\n", ch);
     return eFAILURE;
   }

   int min_hp = (int) (GET_MAX_HIT(ch) / 5);
   min_hp = MIN( min_hp, 25 );

   if( GET_HIT(ch) < min_hp ) {
      send_to_char("You are feeling too weak right now to attempt such a bold maneuver.", ch);
      return eFAILURE;
   }

   if (ch->fighting)
      victim = ch->fighting;
   else {
      send_to_char("You have to already be in combat to do this.  Try using 'backstab'.\r\n", ch);
      return eFAILURE;
   }

   if (IS_MOB(victim) && IS_SET(victim->mobdata->actflags, ACT_HUGE)) {
      send_to_char("You can't backstab someone that HUGE!\n\r", ch);
      return eFAILURE;
   }
       
   if (victim == ch) {
      send_to_char("How can you sneak up on yourself?\n\r", ch);
      return eFAILURE;
   }
    
   // Check the killer/victim
   if ((GET_LEVEL(ch) < G_POWER) || IS_NPC(ch)) {
      if (!can_attack(ch) || !can_be_attacked(ch, victim))
         return eFAILURE;
      }

   if (!ch->equipment[WIELD]) {
      send_to_char("You need to wield a weapon, to make it a success.\n\r", ch);
      return eFAILURE;
   }

   if((ch->equipment[WIELD]->obj_flags.value[3] != 11) &&
     (ch->equipment[WIELD]->obj_flags.value[3] != 9)) {
        send_to_char("Only piercing weapons can be used for backstabbing.\n\r", ch);
        return eFAILURE;
   }

   if (ch == victim->fighting) {
      send_to_char("You can't break away while that person is hitting you!\n\r", ch);
      return eFAILURE;
   }
      
   percent = number(1, 101); // 101% is a complete failure

   specialization = learned / 100;
   learned = learned % 100;

   chance = 70;
   chance += learned / 100;
   chance += (GET_DEX(ch) - GET_DEX(victim)) / 2;
 
//   stop_fighting(ch);
   skill_increase_check(ch, SKILL_CIRCLE, learned, SKILL_INCREASE_MEDIUM);

   act ("You circle around your target...",  ch, 0, 0, TO_CHAR, 0);
   act ("$n circles around $s target...", ch, 0, 0, TO_ROOM, INVIS_NULL);

   WAIT_STATE(ch, PULSE_VIOLENCE * 2);
   
   if (AWAKE(victim) && (percent > chance))
      return damage(ch, victim, 0,TYPE_UNDEFINED, SKILL_BACKSTAB, FIRST);
   else 
   {
      SET_BIT(ch->combat, COMBAT_CIRCLE);
      retval = one_hit(ch, victim, SKILL_BACKSTAB, FIRST);

      if(SOMEONE_DIED(retval))
        return retval;
      
      // Now go for dual backstab
      if ((GET_LEVEL(ch) >= 40) &&
          ((GET_CLASS(ch) == CLASS_THIEF) || (GET_LEVEL(ch) >= ARCHANGEL)))
         if (ch->equipment[SECOND_WIELD])
            if ((ch->equipment[SECOND_WIELD]->obj_flags.value[3] == 11) ||
                (ch->equipment[SECOND_WIELD]->obj_flags.value[3] == 8)) {
               WAIT_STATE(ch, PULSE_VIOLENCE);
               percent = number(1, 101);
               if (AWAKE(victim) &&
                   (percent > has_skill(ch, SKILL_DUAL_BACKSTAB)))
                  damage(ch, victim, 0, TYPE_UNDEFINED, SKILL_BACKSTAB,
                         SECOND);
               else {
                  SET_BIT(ch->combat, COMBAT_CIRCLE);
                  return one_hit(ch, victim, SKILL_BACKSTAB, SECOND);
                  }
               } // end of if that checks weapon's validity
   } // end of else
   return eSUCCESS;
}

int do_trip(CHAR_DATA *ch, char *argument, int cmd)
{
  CHAR_DATA *victim = 0;
  char name[256];
  byte percent;
  int learned, specialization, chance;
  int retval;

  if(IS_MOB(ch) || GET_LEVEL(ch) >= ARCHANGEL)
    learned = 75;
  else if(!(learned = has_skill(ch, SKILL_TRIP))) {
    send_to_char("You should learn how to trip first!\r\n", ch);
    return eFAILURE;
  }

  one_argument(argument, name);

  if(!(victim = get_char_room_vis(ch, name))) {
    if(ch->fighting)
      victim = ch->fighting;
    else {
      send_to_char( "Trip whom?\n\r", ch );
      return eFAILURE;
    }
  }

  if(ch->in_room != victim->in_room) {
    send_to_char("That person seems to have left.\n\r", ch);
    return eFAILURE;
  }
  
  if(victim == ch) {
    send_to_char("(I could let you trip yourself if I wanted to.)\n\r", ch);
    return eFAILURE;
  }

  if(!can_be_attacked(ch, victim) || !can_attack(ch))
    return eFAILURE;

   specialization = learned / 100;
   learned = learned % 100;

   chance = 60;
   chance += learned / 100;
   chance += (GET_DEX(ch) - GET_DEX(victim)) / 2;
 
   skill_increase_check(ch, SKILL_TRIP, learned, SKILL_INCREASE_MEDIUM);

  if(affected_by_spell(victim, SPELL_IRON_ROOTS)) {
    act("You try to trip $N but tree roots around $S legs keep him upright.", ch, 0, victim, TO_CHAR, 0);
    act("$n trips you but the roots around your legs keep you from falling.", ch, 0, victim, TO_VICT, 0);
    act("The tree roots support $N keeping $M from falling after $n's trip.", ch, 0, victim, TO_ROOM, NOTVICT);
    WAIT_STATE(ch, 2 * PULSE_VIOLENCE);
    return eFAILURE;
  }

  // 101% is a complete failure
  percent = number(1, 101);

  if(percent > chance) {
    act("$n fumbles clumsily as $s attempts to trip you!", ch, NULL, victim, TO_VICT, 0 );
    act("You fumble the trip!", ch, NULL, victim, TO_CHAR , 0);
    act("$n fumbles as $s tries to trip $N!", ch, NULL, victim, TO_ROOM, NOTVICT );
    WAIT_STATE(ch, PULSE_VIOLENCE*1);
    retval = damage(ch, victim, 0, TYPE_UNDEFINED, SKILL_TRIP, 0);
  }
  else {
    act("$n trips you and you go down!", ch, NULL, victim, TO_VICT , 0);
    act("You trip $N and $N goes down!", ch, NULL, victim, TO_CHAR , 0);
    act("$n trips $N and $N goes down!", ch, NULL, victim, TO_ROOM, NOTVICT );
    if(GET_POS(victim) > POSITION_SITTING)
       GET_POS(victim) = POSITION_SITTING;
    SET_BIT(victim->combat, COMBAT_BASH2);
    WAIT_STATE(ch, PULSE_VIOLENCE*3);
    WAIT_STATE(victim, PULSE_VIOLENCE*2);
    retval = damage(ch, victim, 0, TYPE_UNDEFINED, SKILL_TRIP, 0);
  }
  return retval;
}

int do_sneak(CHAR_DATA *ch, char *argument, int cmd)
{
   affected_type af;
   int learned;

   if(IS_MOB(ch) || GET_LEVEL(ch) >= ARCHANGEL)
      learned = 75;
   else if(!(learned = has_skill(ch, SKILL_SNEAK))) {
      send_to_char("You just don't seem like the sneaky type.\r\n", ch);
      return eFAILURE;
   }

   if (IS_AFFECTED(ch, AFF_SNEAK))  
   {
      affect_from_char(ch, SKILL_SNEAK);
      if (cmd != 10) {
         send_to_char("You won't be so sneaky anymore.\n\r", ch);
         return eFAILURE;
      }
   }

   do_hide(ch, "", 9);

   send_to_char("You try to move silently for a while.\n\r", ch);

   skill_increase_check(ch, SKILL_SNEAK, learned, SKILL_INCREASE_HARD);

   af.type = SKILL_SNEAK;
   af.duration = MAX(5, GET_LEVEL(ch) / 2);
   af.modifier = 0;
   af.location = APPLY_NONE;
   af.bitvector = AFF_SNEAK;
   affect_to_char(ch, &af);
   return eSUCCESS;
}

int do_stalk(CHAR_DATA *ch, char *argument, int cmd)
{
  int percent, learned, chance;
  char name[MAX_STRING_LENGTH];
  CHAR_DATA *leader;

  if(!(*argument)) {
    send_to_char("Pick a name, any name.\n\r", ch);
    return eFAILURE;
  }

  one_argument(argument, name);

  if(!(leader = get_char_room_vis(ch, name))) {
    send_to_char("I see no person by that name here!\n\r", ch);
    return eFAILURE;
  }

  if(leader == ch) {
    if(!ch->master) 
      send_to_char("You are already following yourself.\n\r", ch);
    else if(IS_AFFECTED(ch, AFF_GROUP)) 
      send_to_char("You must first abandon your group.\n\r",ch);
    else 
      stop_follower(ch, 1);
    return eFAILURE;
  }

  if(IS_MOB(ch) || GET_LEVEL(ch) >= ARCHANGEL)
    learned = 75;
  else if(!(learned = has_skill(ch, SKILL_STALK))) {
    send_to_char("I bet you think you're a thief ;)\n\r", ch);
    return eFAILURE;
  } 

  chance = 75;

  percent = number(1,101);

  skill_increase_check(ch, SKILL_STALK, learned, SKILL_INCREASE_EASY);

  if(percent > chance)
    do_follow(ch, argument, 9);

  else { 
    do_follow(ch, argument, 10);
    do_sneak(ch, argument, 10);
  } 
  return eSUCCESS;
}

int do_hide(CHAR_DATA *ch, char *argument, int cmd)
{
   for(char_data * curr = world[ch->in_room].people;
       curr;
       curr = curr->next_in_room)
   {
      if(curr->fighting == ch) {
         send_to_char("In the middle of combat?!  Impossible!\r\n", ch);
         return eFAILURE;
      }
   }

   send_to_char("You attempt to hide yourself.\n\r", ch);

   if ( ! IS_AFFECTED(ch, AFF_HIDE) )
      SET_BIT(ch->affected_by, AFF_HIDE);
   return eSUCCESS;
}

// steal an ITEM... not gold
int do_steal(CHAR_DATA *ch, char *argument, int cmd)
{
  CHAR_DATA *victim;
  struct obj_data *obj, *loop_obj, *next_obj;
  struct affected_type pthiefaf, *paf;
  char victim_name[240];
  char obj_name[240];
  char buf[240];
  int percent, learned, specialization, chance;
  int eq_pos;
  int _exp;
  int retval;
  obj_data * has_item = NULL;
  bool ohoh = FALSE;

  extern struct index_data *obj_index;

  argument = one_argument(argument, obj_name);
  one_argument(argument, victim_name);
  if (ch->c_class != CLASS_THIEF)
  {
	send_to_char("You are not experienced within that field.\r\n",ch);
	return eFAILURE;
  }
  pthiefaf.type = FUCK_PTHIEF;
  pthiefaf.duration = 10;
  pthiefaf.modifier = 0;
  pthiefaf.location = APPLY_NONE;
  pthiefaf.bitvector = 0;

  if(!(victim = get_char_room_vis(ch, victim_name))) {
    send_to_char("Steal what from who?\n\r", ch);
    return eFAILURE;
  }
  else if (victim == ch) {
    send_to_char("Got it!\n\rYou receive 30000000000 exps.\n\r", ch);
    return eFAILURE;
  }

  if(IS_MOB(ch))
    learned = 75;
  else learned = has_skill(ch, SKILL_STEAL);

  specialization = learned / 100;
  learned = learned % 100;

  if(GET_POS(victim) == POSITION_DEAD) {
     send_to_char("Don't steal from dead PC's\r\n", ch);
     return eFAILURE;
  }

  if(IS_MOB(ch))
     return eFAILURE;

  if((GET_LEVEL(ch) < (GET_LEVEL(victim) - 10))) {
    send_to_char("That person is far too experienced for you to steal from.\r\n", ch);
    return eFAILURE;
  }

  if(IS_SET(world[ch->in_room].room_flags, SAFE)) {
    send_to_char("No stealing permitted in safe areas!\n\r", ch);
    return eFAILURE;
  }
    
  if(IS_SET(world[ch->in_room].room_flags, ARENA)) {
     send_to_char("Do what!? This is an Arena, go kill someone!\n\r", ch);
     return eFAILURE;
  }

  if(IS_AFFECTED(ch, AFF_CHARM)) {
     return do_say(ch, "Nice try.", 9);
  }

  if(victim->fighting) {
     send_to_char("You can't get close enough because of the fight.\n\r", ch);
     return eFAILURE;
  }

/*  if(!IS_NPC(victim) &&
    !(victim->desc) && !affected_by_spell(victim, FUCK_PTHIEF) ) {
    send_to_char("That person isn't really there.\n\r", ch);
    return eFAILURE;
  }*/

  WAIT_STATE(ch, 10); /* It takes TIME to steal */

  /* 101% is a complete failure */
  percent = number(1,101);

  percent += AWAKE(victim) ? 10 : -50;
  percent += ((GET_LEVEL(victim)-GET_LEVEL(ch))/2); // take level into account
 
  if(GET_POS(victim) <= POSITION_SLEEPING &&
     GET_POS(victim) != POSITION_STUNNED)
    percent = -1; /* ALWAYS SUCCESS */

  if(GET_LEVEL(victim) > IMMORTAL) /* NO NO With Imp's and Shopkeepers! */
    percent = 101; /* Failure */

  if(learned)
    chance = 60;
  else chance = 0;

  if((obj = get_obj_in_list_vis(ch, obj_name, victim->carrying))) 
  {
    if(IS_SET(obj->obj_flags.extra_flags, ITEM_SPECIAL)) 
    {
      send_to_char("That item is protected by the gods.\n\r", ch);
      return eFAILURE;
    }
  
    // obj found in inventory
    percent += GET_OBJ_WEIGHT(obj); // Make heavy harder

    if(learned)
      skill_increase_check(ch, SKILL_STEAL, learned, SKILL_INCREASE_HARD);

    if (percent > chance) 
    {
      set_cantquit( ch, victim );
      send_to_char("Oops..", ch);
      ohoh = TRUE;
      if(!number(0, 4)) {
        act("$n tried to steal something from you!", ch, 0, victim, TO_VICT, 0);
        act("$n tries to steal something from $N.", ch, 0, victim, TO_ROOM, INVIS_NULL|NOTVICT);
      }
    } 
    else 
    { /* Steal the item */
      if ((IS_CARRYING_N(ch) + 1 < CAN_CARRY_N(ch))) 
      {
        if ((IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(obj)) < CAN_CARRY_W(ch)) 
        {
          move_obj(obj, ch);

          if(!IS_NPC(victim) || (IS_SET(victim->mobdata->actflags,ACT_NICE_THIEF)))
            _exp = GET_OBJ_WEIGHT(obj);
          else _exp = (GET_OBJ_WEIGHT(obj) * GET_LEVEL(victim));

          if(GET_POS(victim) <= POSITION_SLEEPING)  
            _exp = 1;

          GET_EXP(ch) += _exp; /* exp for stealing :) */
          sprintf(buf,"You receive %d exps.\n\r", _exp);
          send_to_char("Got it!\n\r", ch);
          send_to_char(buf, ch);
          if(!IS_NPC(victim)) 
          {
            do_save(victim, "", 666);
            do_save(ch, "", 666);
            if(!AWAKE(victim))
            {
              if(number(1, 3) == 1)
                send_to_char("You dream of someone stealing your eq.\r\n", victim);
              if((paf = affected_by_spell(victim, SPELL_SLEEP)) && paf->modifier == 1)
              {
                paf->modifier = 0; // make sleep no longer work
              }
              // if i'm not a thief, or if I fail dex-roll wake up victim
              if(GET_CLASS(ch) != CLASS_THIEF || number(1, 100) > GET_DEX(ch))
              {
                send_to_char("Oops...\r\n", ch);
                do_wake(ch, GET_NAME(victim), 9);
              }
            }
            // if victim isn't a pthief
            if(!affected_by_spell(victim, FUCK_PTHIEF) ) 
            {
              set_cantquit( ch, victim );
              if(affected_by_spell(ch, FUCK_PTHIEF))
              {
                affect_from_char(ch, FUCK_PTHIEF);
                affect_to_char(ch, &pthiefaf);
              }
              else
                affect_to_char(ch, &pthiefaf);
            }
          }
          if(!IS_NPC(victim))
          {
            sprintf(log_buf,"%s stole %s[%d] from %s",
                 GET_NAME(ch), obj->short_description,  
                 obj_index[obj->item_number].virt, GET_NAME(victim));
            log(log_buf, ANGEL, LOG_MORTAL);
            for(loop_obj = obj->contains; loop_obj; loop_obj = loop_obj->next_content)
              logf(ANGEL, LOG_MORTAL, "The %s contained %s[%d]", 
                          obj->short_description,
                          loop_obj->short_description,
                          obj_index[loop_obj->item_number].virt);
          }
          obj_from_char(obj);
          has_item = search_char_for_item(ch, obj->item_number);
          obj_to_char(obj, ch);
          if(IS_SET(obj->obj_flags.more_flags, ITEM_NO_TRADE) ||
                ( IS_SET(obj->obj_flags.more_flags, ITEM_UNIQUE) && has_item )
            )
          {
            csendf(ch, "Whoa!  The %s poofed into thin air!\r\n", obj->short_description);
            extract_obj(obj);
          }
          // check for no_trade inside containers
          else for(loop_obj = obj->contains; loop_obj; loop_obj = next_obj)
          {
             // this is 'else' since if the container was no_trade everything in it
             // has already been extracted
             next_obj = loop_obj->next_content;

             if(IS_SET(loop_obj->obj_flags.more_flags, ITEM_NO_TRADE) ||
                ( IS_SET(obj->obj_flags.more_flags, ITEM_UNIQUE) && has_item )
               ) 
             {
                csendf(ch, "Whoa!  The %s inside the %s poofed into thin air!\r\n",
                           loop_obj->short_description, obj->short_description);
                extract_obj(loop_obj);
             }
          }
        }
        else
          send_to_char("You cannot carry that much weight.\n\r", ch);
      } else
        send_to_char("You cannot carry that many items.\n\r", ch);
    }
  }
  else // not in inventory
  {
    for(eq_pos = 0; (eq_pos < MAX_WEAR); eq_pos++)
    {
      if(victim->equipment[eq_pos] &&
        (isname(obj_name, victim->equipment[eq_pos]->name)) &&
        CAN_SEE_OBJ(ch,victim->equipment[eq_pos])) 
      {
        obj = victim->equipment[eq_pos];
        break;
      }
    }

    if(obj) 
    { // They're wearing it!
     int wakey = 100;
       switch (eq_pos)
	{
	  case WEAR_FINGER_R:
	  case WEAR_FINGER_L:
	  case WEAR_NECK_1:
	  case WEAR_NECK_2:
	  case WEAR_EAR_L:
	  case WEAR_EAR_R:
	  case WEAR_WRIST_R:
	  case WEAR_WRIST_L:
	     wakey = 30;
	     break;
	  case WEAR_HANDS:
	  case WEAR_FEET:
	  case WEAR_WAISTE:
	  case WEAR_HEAD:
	  case WIELD:
	  case SECOND_WIELD:
	  case WEAR_LIGHT:
	  case HOLD:
	     wakey = 60;
	     break;
	  case WEAR_BODY:
	  case WEAR_LEGS:
	  case WEAR_ABOUT:
	  case WEAR_FACE:
	  case WEAR_ARMS:
	    wakey = 90;
	    break;
	  default:
	    send_to_char("Something just screwed up. Tell an imm what you did.\r\n",ch);
	    return eFAILURE;	  
	};
	wakey -= GET_DEX(ch) /2;
      if(IS_SET(obj->obj_flags.extra_flags, ITEM_SPECIAL)) 
      {
        send_to_char("That item is protected by the gods.\n\r", ch);
        return eFAILURE;
      }

      if(GET_POS(victim) > POSITION_SLEEPING ||
         GET_POS(victim) == POSITION_STUNNED) 
      {
        send_to_char("Steal the equipment now? Impossible!\n\r", ch);
        return eFAILURE;
      }
      else 
      {
        act("You unequip $p and steal it.", ch, obj ,0, TO_CHAR, 0);
        act("$n steals $p from $N.",ch,obj,victim,TO_ROOM, NOTVICT);
        obj_to_char(unequip_char(victim, eq_pos), ch);
        if(!IS_NPC(victim) || (IS_SET(victim->mobdata->actflags,ACT_NICE_THIEF)))
          _exp = GET_OBJ_WEIGHT(obj);
        else
          _exp = (GET_OBJ_WEIGHT(obj) * GET_LEVEL(victim));
        if(GET_POS(victim) <= POSITION_SLEEPING)    _exp = 1; 
        GET_EXP(ch) += _exp;                   /* exp for stealing :) */ 
        sprintf(buf,"You receive %d exps.\n\r", _exp);
        send_to_char(buf, ch);
        sprintf(buf,"%s stole from %s while victim was asleep",
                GET_NAME(ch), GET_NAME(victim));
        log(buf, ANGEL, LOG_MORTAL);
        if(!IS_MOB(victim)) 
        {
          do_save(victim, "", 666);
          do_save(ch, "", 666);
          if(!AWAKE(victim))
          {
            if(number(1, 3) == 1)
              send_to_char("You dream of someone stealing your eq.\r\n", victim);
            if((paf = affected_by_spell(victim, SPELL_SLEEP)) && paf->modifier == 1)
            {
              paf->modifier = 0; // make sleep no longer work
            }
            // if i'm not a thief, or if I fail dex-roll wake up victim
            if(number(1,101) > wakey)
            {
              send_to_char("Oops...\r\n", ch);
              do_wake(ch, GET_NAME(victim), 9);
            }
          }

          // You don't get a thief flag from stealing from a pthief
          if(!affected_by_spell(victim, FUCK_PTHIEF)) 
          {
            set_cantquit( ch, victim );
            if(affected_by_spell(ch, FUCK_PTHIEF))
            {
              affect_from_char(ch, FUCK_PTHIEF);
              affect_to_char(ch, &pthiefaf);
            }
            else
              affect_to_char(ch, &pthiefaf);
          }  
        } // !is_npc
        obj_from_char(obj);
        has_item = search_char_for_item(ch, obj->item_number);
        obj_to_char(obj, ch);
        if(IS_SET(obj->obj_flags.more_flags, ITEM_NO_TRADE) ||
            ( IS_SET(obj->obj_flags.more_flags, ITEM_UNIQUE) && has_item )
          )
        {
          send_to_char("Whoa!  If poofed into thin air!\r\n", ch);
          extract_obj(obj);
        }
        else for(loop_obj = obj->contains; loop_obj; loop_obj = next_obj)
        {
           // this is 'else' since if the container was no_trade everything in it
           // has already been extracted
           next_obj = loop_obj->next_content;

           if(IS_SET(loop_obj->obj_flags.more_flags, ITEM_NO_TRADE) ||
               ( IS_SET(obj->obj_flags.more_flags, ITEM_UNIQUE) && has_item )
             ) 
           {
              csendf(ch, "Whoa!  The %s inside the %s poofed into thin air!\r\n",
                         loop_obj->short_description, obj->short_description);
              extract_obj(loop_obj);
           }
        }
      } // else
    } // if(obj)
    else
    { // they don't got it
      act("$E has not got that item.",ch,0,victim,TO_CHAR, 0);
      return eFAILURE;
    }
  } // of else, not in inventory

  if (ohoh && IS_NPC(victim) && AWAKE(victim) && GET_LEVEL(ch)<ANGEL)
  {
    if (IS_SET(victim->mobdata->actflags, ACT_NICE_THIEF)) 
    {
      sprintf(buf, "%s is a bloody thief.", GET_SHORT(ch));
      do_shout(victim, buf, 0);
    } else 
    {
      retval = attack(victim, ch, TYPE_UNDEFINED);
      retval = SWAP_CH_VICT(retval);
      return retval;
    }
  }
  return eSUCCESS;
}

// Steal gold 
int do_pocket(CHAR_DATA *ch, char *argument, int cmd)
{
  CHAR_DATA *victim;
  struct affected_type pthiefaf;
  char victim_name[240];
  char buf[240];
  int percent, learned, chance, specialization;
  int gold;
  int _exp;
  int retval;
  bool ohoh = FALSE;

  one_argument(argument, victim_name);

  pthiefaf.type = FUCK_PTHIEF;
  pthiefaf.duration = 20;
  pthiefaf.modifier = 0;
  pthiefaf.location = APPLY_NONE;
  pthiefaf.bitvector = 0;

  if(!(victim = get_char_room_vis(ch, victim_name))) {
    send_to_char("Steal what from who?\n\r", ch);
    return eFAILURE;
  }
  else if (victim == ch) {
    send_to_char("Got it!\n\rYou receive 30000000000 exps.\n\r", ch);
    return eFAILURE;
  }

  if(IS_MOB(ch))
    learned = 75;
  else learned = has_skill(ch, SKILL_POCKET);

  specialization = learned / 100;
  learned = learned % 100;

  if(GET_POS(victim) == POSITION_DEAD) {
     send_to_char("Don't steal from dead PC's\r\n", ch);
     return eFAILURE;
  }

  if(IS_MOB(ch))
     return eFAILURE;

  if((GET_LEVEL(ch) < (GET_LEVEL(victim) - 10))) {
    send_to_char("That person is far too experienced to steal from.\r\n", ch);
    return eFAILURE;
  }

  if(IS_SET(world[ch->in_room].room_flags, SAFE)) {
    send_to_char("No stealing permitted in safe areas!\n\r", ch);
    return eFAILURE;
  }
    
  if(IS_SET(world[ch->in_room].room_flags, ARENA)) {
     send_to_char("Do what!? This is an Arena, go kill someone!\n\r", ch);
     return eFAILURE;
  }

  if(IS_AFFECTED(ch, AFF_CHARM)) {
     return do_say(ch, "Nice try.", 9);
  }

  if(victim->fighting) {
     send_to_char("You can't get close enough because of the fight.\n\r", ch);
     return eFAILURE;
  }


  /*if(!IS_NPC(victim) &&
    !(victim->desc) && !affected_by_spell(victim, FUCK_PTHIEF) ) {
    send_to_char("That person isn't really there.\n\r", ch);
    return eFAILURE;
  }
*/
  WAIT_STATE(ch, 10); /* It takes TIME to steal */

  /* 101% is a complete failure */
  percent = number(1,101);

  percent += AWAKE(victim) ? 10 : -50;
  percent += ((GET_LEVEL(victim)-GET_LEVEL(ch))/2); // take level into account
 
  if(GET_POS(victim) <= POSITION_SLEEPING &&
     GET_POS(victim) != POSITION_STUNNED)
    percent = -1; /* ALWAYS SUCCESS */

  if(GET_LEVEL(victim) > IMMORTAL) /* NO NO With Imp's and Shopkeepers! */
    percent = 101; /* Failure */

  if(learned)
    chance = learned;
  else chance = 0;

  if(learned)
    skill_increase_check(ch, SKILL_POCKET, learned, SKILL_INCREASE_MEDIUM);

  if (percent > chance) 
  {
    set_cantquit( ch, victim );
    send_to_char("Oops..\r\n", ch);
    ohoh = TRUE;
	    if(!number(0, 6)) {
      act("You discover that $n has $s hands in your wallet.", ch,0,victim,TO_VICT, 0);
      act("$n tries to steal gold from $N.", ch, 0, victim, TO_ROOM, NOTVICT|INVIS_NULL);
    }
  } else 
  {
    // Steal some gold coins
    percent = learned / 10 + number(-1,1);
    gold = (int) ((float)GET_GOLD(victim)*(float)((float)percent/100.0));
//    gold = MIN(10000, gold);
    if (gold > 0) {
      GET_GOLD(ch)     += gold;
      GET_GOLD(victim) -= gold;
      if(GET_POS(victim) <= POSITION_SLEEPING) _exp = 1;
      else if(!IS_NPC(victim)) _exp = 0;
      else if(IS_SET(victim->mobdata->actflags, ACT_NICE_THIEF)) _exp = 1; 
      else _exp = gold;
      GET_EXP(ch)      += _exp;
      sprintf(buf, "Bingo! You got %d gold coins.\n\r", gold);
      send_to_char(buf, ch);
      sprintf(buf,"You receive %d exps.\n\r", _exp);
      send_to_char(buf, ch);
      if(!IS_NPC(victim)) 
      {
        do_save(victim, "", 666);
        do_save(ch, "", 666);
        if(!affected_by_spell(victim, FUCK_PTHIEF) ) 
        {
          set_cantquit( ch, victim );
          if(affected_by_spell(ch, FUCK_PTHIEF))
          {
            affect_from_char(ch, FUCK_PTHIEF);
            affect_to_char(ch, &pthiefaf);
          }
          else
            affect_to_char(ch, &pthiefaf);
        }
      }
      if ((GET_LEVEL(ch)<ANGEL) && (!IS_NPC(victim))) 
      {
        sprintf(log_buf,"%s stole %d gold from %s", GET_NAME(ch), gold, GET_NAME(victim));
        log(log_buf, ANGEL, LOG_MORTAL);
      }
    } else 
    {
      send_to_char("You couldn't get any gold...\n\r", ch);
    }
  }

  if (ohoh && IS_NPC(victim) && AWAKE(victim) && GET_LEVEL(ch)<ANGEL)
  {
    if (IS_SET(victim->mobdata->actflags, ACT_NICE_THIEF)) 
    {
      sprintf(buf, "%s is a bloody thief.", GET_SHORT(ch));
      do_shout(victim, buf, 0);
    } else 
    {
      retval = attack(victim, ch, TYPE_UNDEFINED);
      retval = SWAP_CH_VICT(retval);
      return retval;
    }
  }
  return eSUCCESS;
}

int do_pick(CHAR_DATA *ch, char *argument, int cmd)
{
   int percent, learned, specialization, chance;
   int door, other_room;
   char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
   struct room_direction_data *back;
   struct obj_data *obj;
   CHAR_DATA *victim;

   argument_interpreter(argument, type, dir);

   percent = number(1, 101); // 101% is a complete failure

   learned = has_skill(ch, SKILL_PICK_LOCK);
   if(!learned) {
      send_to_char("You don't know how to pick locks!\r\n", ch);
      return eFAILURE;
   }
   specialization = learned / 100;
   learned = learned % 100;

   chance = 75;

   // TODO - add lockpics and make things happen with this skill..

   if (percent > chance) {
      send_to_char("You failed to pick the lock.\n\r", ch);
      WAIT_STATE(ch, PULSE_VIOLENCE);
      return eFAILURE;
    }

   if (!*type)
      send_to_char("Pick what?\n\r", ch);
   else if (generic_find(argument, (FIND_OBJ_INV | FIND_OBJ_ROOM), ch, &victim, &obj))

  // this is an object

  if (obj->obj_flags.type_flag != ITEM_CONTAINER)
      send_to_char("That's not a container.\n\r", ch);
  else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
      send_to_char("Silly - it ain't even closed!\n\r", ch);
  else if (obj->obj_flags.value[2] < 0)
      send_to_char("Odd - you can't seem to find a keyhole.\n\r", ch);
  else if (!IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
      send_to_char("Oho! This thing is NOT locked!\n\r", ch);
  else if (IS_SET(obj->obj_flags.value[1], CONT_PICKPROOF))
      send_to_char("It resists your attempts at picking it.\n\r", ch);
  else
  {
      skill_increase_check(ch, SKILL_PICK_LOCK, learned, SKILL_INCREASE_MEDIUM);

      REMOVE_BIT(obj->obj_flags.value[1], CONT_LOCKED);
      send_to_char("*Click*\n\r", ch);
      act("$n fiddles with $p.", ch, obj, 0, TO_ROOM, 0);
  }
    else if ((door = find_door(ch, type, dir)) >= 0)
  if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
      send_to_char("That's absurd.\n\r", ch);
  else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
      send_to_char("You realize that the door is already open.\n\r", ch);
  else if (EXIT(ch, door)->key < 0)
      send_to_char("You can't seem to spot any lock to pick.\n\r", ch);
  else if (!IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
      send_to_char("Oh.. it wasn't locked at all.\n\r", ch);
  else if (IS_SET(EXIT(ch, door)->exit_info, EX_PICKPROOF))
      send_to_char("You seem to be unable to pick ths lock.\n\r", ch);
  else
  {
      skill_increase_check(ch, SKILL_PICK_LOCK, learned, SKILL_INCREASE_MEDIUM);

      REMOVE_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
      if (EXIT(ch, door)->keyword)
    act("$n skillfully picks the lock of the $F.", ch, 0,
        EXIT(ch, door)->keyword, TO_ROOM, 0);
      else
    act("$n picks the lock of the.", ch, 0, 0, TO_ROOM,
      INVIS_NULL);
      send_to_char("The lock quickly yields to your skills.\n\r", ch);
      /* now for unlocking the other side, too */
      if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
      if ( ( back = world[other_room].dir_option[rev_dir[door]] ) != 0 )
        if (back->to_room == ch->in_room)
      REMOVE_BIT(back->exit_info, EX_LOCKED);
  }
  return eSUCCESS;
}


int do_slip(struct char_data *ch, char *argument, int cmd)
{
   char obj_name[200], vict_name[200], buf[200];
   char arg[MAX_INPUT_LENGTH];
   int amount, percent;
   byte learned;
   struct char_data *vict;
   struct obj_data *obj, *tmp_object, *container;

   percent = GET_LEVEL(ch) >= ARCHANGEL ? 0 : number(1, 101);

   if(IS_MOB(ch))
      learned = 75;
   else learned = has_skill(ch, SKILL_SLIP);

    if(!IS_MOB(ch) && IS_AFFECTED(ch, AFF_CANTQUIT) && affected_by_spell(ch, FUCK_PTHIEF) ) { 
      send_to_char("Your criminal acts prohibit it.\n\r", ch);
      return eFAILURE;
    }

   argument = one_argument(argument, obj_name);

   if (is_number(obj_name)) { 
      if (strlen(obj_name) > 7) {
         send_to_char("Number field too large.\n\r", ch);
         return eFAILURE;
         }
   
      amount   = atoi(obj_name);
      argument = one_argument(argument, arg);
      
      if (str_cmp("coins", arg) && str_cmp("coin", arg)) { 
         send_to_char("Sorry, you can't do that (yet)...\n\r",ch);
         return eFAILURE;
         }
      if (amount <= 0) { 
         send_to_char("Sorry, you can't do that!\n\r",ch);
         return eFAILURE;
         }
      if ((GET_GOLD(ch) < (uint32)amount) && (GET_LEVEL(ch) < DEITY)) { 
         send_to_char("You haven't got that many coins!\n\r",ch);
         return eFAILURE;
         }
    
      argument = one_argument(argument, vict_name);
    
      if (!*vict_name) { 
         send_to_char("To who?\n\r", ch);
         return eFAILURE;
         }
      if (!(vict = get_char_room_vis(ch, vict_name))) {
         send_to_char("To who?\n\r", ch);
         return eFAILURE;
         }
      if (ch == vict) {
         send_to_char("To yourself?!  Very cute...\n\r", ch);
         return eFAILURE;
      }    
      // Failure
      if (percent > 60) {
         send_to_char("Whoops!  You dropped them.\n\r", ch);
         if (GET_LEVEL(ch) >= IMMORTAL) { 
            sprintf(buf, "%s slips %d coins to %s and fumbles.", GET_NAME(ch),
              amount, GET_NAME(vict));
            special_log(buf);
            }
      
         act("$n tries to slip you some coins, but $e accidentally drops "
             "them.\n\r", ch, 0, vict, TO_VICT, 0);
         act("$n tries to slip $N some coins, but $e accidentally drops "
             "them.\n\r", ch, 0, vict, TO_ROOM, NOTVICT);
    
         if (IS_NPC(ch) || (GET_LEVEL(ch) < DEITY))
            GET_GOLD(ch) -= amount;
      
         tmp_object = create_money(amount);
         obj_to_room(tmp_object, ch->in_room);
         do_save(ch, "", 9);
         } // failure
    
      // Success
      else {
         send_to_char("Ok.\n\r", ch);
         if (GET_LEVEL(ch) >= IMMORTAL) { 
            sprintf(buf, "%s gives %d coins to %s", GET_NAME(ch), amount,
                    GET_NAME(vict));
            special_log(buf);
            } 
      
         sprintf(buf, "%s slips you %d gold coins.\n\r", PERS(ch, vict),
           amount);
         act(buf, ch, 0, vict, TO_VICT, GODS);
         act("$n slips some gold to $N.", ch, 0, vict, TO_ROOM, GODS|NOTVICT);
    
         if (IS_NPC(ch) || (GET_LEVEL(ch) < DEITY))
            GET_GOLD(ch) -= amount;
      
         GET_GOLD(vict) += amount;
         do_save(ch, "", 9);
         save_char_obj(vict);
         }
    
      return eFAILURE;
      } // if (is_number)

   argument = one_argument(argument, vict_name);

   if (!*obj_name || !*vict_name) {
      send_to_char("Slip what to who?\n\r", ch);
      return eFAILURE;
      }
      
   if (!(obj = get_obj_in_list_vis(ch, obj_name, ch->carrying))) {
      send_to_char("You do not seem to have anything like that.\n\r", ch);
      return eFAILURE;
      }
      
   if (IS_SET(obj->obj_flags.extra_flags, ITEM_SPECIAL)) {
      send_to_char("That sure would be a fucking stupid thing to do.\n\r", ch);
      return eFAILURE;
      }
     
   if(IS_SET(obj->obj_flags.more_flags, ITEM_NO_TRADE)) {
      send_to_char("You can't seem to get the item to leave you.\n\r", ch);
      return eFAILURE;
   }
 
   if (IS_SET(obj->obj_flags.extra_flags, ITEM_NODROP))
      if (GET_LEVEL(ch) < DEITY) {
         send_to_char("You can't let go of it! Yeech!!\n\r", ch);
         return eFAILURE;
         }
      else
         send_to_char("This item is NODROP btw.\n\r", ch);

   if(GET_ITEM_TYPE(obj) == ITEM_CONTAINER)
   {
     send_to_char("You would ruin it!\n\r", ch);
     return eFAILURE;
   }

   // We're going to slip the item into our container instead
   if((container = get_obj_in_list_vis(ch, vict_name, ch->carrying)))
   {
      if(GET_ITEM_TYPE(container) != ITEM_CONTAINER) {
         send_to_char("That's not a container.\r\n", ch);
         return eFAILURE;
      }
      if(IS_SET(container->obj_flags.value[1], CONT_CLOSED)) {
         send_to_char("It seems to be closed.\r\n", ch);
         return eFAILURE;
      }
      if((container->obj_flags.weight + obj->obj_flags.weight) >=
          container->obj_flags.value[0]) {
         send_to_char("It won't fit........cheater.\r\n", ch);
         return eFAILURE;
      }
      if (percent > learned) { // fail
         act("$n tries to slip $p in $P, but you notice.", ch, obj,
             container, TO_ROOM, 0);
      }
      else act("$n slips $p in $P.", ch, obj, container, TO_ROOM, GODS);
      move_obj(obj, container);
      // fix weight (move_obj doesn't re-add it, but it removes it)
      IS_CARRYING_W(ch) += GET_OBJ_WEIGHT(obj);
      send_to_char("Ok.\r\n", ch);
      return eSUCCESS;      
   }
   if (!(vict = get_char_room_vis(ch, vict_name))) {
      send_to_char("No one by that name around here.\n\r", ch);
      return eFAILURE;
      }

   if (IS_NPC(vict) && mob_index[vict->mobdata->nr].non_combat_func == shop_keeper) {
      act("$N graciously refuses your gift.", ch, 0, vict, TO_CHAR, 0);
      return eFAILURE;
      }

   if (ch == vict) {
      send_to_char("To yourself?!  Very cute...\n\r", ch);
      return eFAILURE;
   }    

   if(affected_by_spell(ch, FUCK_PTHIEF) && !vict->desc) {
      send_to_char("Now WHY would a thief slip something to a "
             "linkdead char..?\n\r", ch);
      return eFAILURE;
      }

   if ((1 + IS_CARRYING_N(vict)) > CAN_CARRY_N(vict)) {
      act("$N seems to have $S hands full.", ch, 0, vict, TO_CHAR, 0);
      return eFAILURE;
      }
      
   if (obj->obj_flags.weight + IS_CARRYING_W(vict) > CAN_CARRY_W(vict)) {
      act("$E can't carry that much weight.", ch, 0, vict, TO_CHAR, 0);
      return eFAILURE;
      }
    
   if(IS_SET(obj->obj_flags.more_flags, ITEM_UNIQUE)) {
     if(search_char_for_item(vict, obj->item_number)) {
        send_to_char("The item's uniqueness prevents it!\r\n", ch);
        return eFAILURE;
     }
   }

   skill_increase_check(ch, SKILL_SLIP, learned, SKILL_INCREASE_EASY);

   // Failure
   if (percent > learned) {
      if(obj_index[obj->item_number].virt == 393) {
         send_to_char("Whoa, you almost dropped your hot potato!\n\r", ch);
         return eFAILURE;
      }

      if (GET_LEVEL(ch) >= IMMORTAL  && GET_LEVEL(ch) <= DEITY ) {
         sprintf(buf, "%s slips %s to %s and fumbles it.", GET_NAME(ch),
                 obj->short_description, GET_NAME(vict));
         special_log(buf);
         }
      
      move_obj(obj, ch->in_room);

      act("$n tries to slip you something, but $e accidentally drops "
          "it.\n\r", ch, 0, vict, TO_VICT, 0);
      act("$n tries to slip $N something, but $e accidentally drops "
          "it.\n\r", ch, 0, vict, TO_ROOM, NOTVICT);
      send_to_char("Whoops!  You dropped it.\n\r", ch);
      do_save(ch, "", 9);
      }
    
   // Success
   else {
      if (GET_LEVEL(ch) >= IMMORTAL  && GET_LEVEL(ch) <= DEITY ) {
         sprintf(buf, "%s slips %s to %s.", GET_NAME(ch),
                 obj->short_description, GET_NAME(vict));
         special_log(buf);
         }
   
      move_obj(obj, vict);
      act("$n slips $p to $N.", ch, obj, vict, TO_ROOM, GODS|NOTVICT);
      act("$n slips you $p.", ch, obj, vict, TO_VICT, GODS);
      send_to_char("Ok.\n\r", ch);
      do_save(ch, "", 9);
      save_char_obj(vict);
      }
  return eSUCCESS;
}

int do_vitalstrike(struct char_data *ch, char *argument, int cmd)
{
  struct affected_type af;
  int learned, percent, specialization, chance; 
    
  if(affected_by_spell(ch, SKILL_VITAL_STRIKE) && GET_LEVEL(ch) < IMMORTAL) {
    send_to_char("Your body is still recovering from your last vital strike technique.\r\n", ch);
    return eFAILURE;
  }
    
  if(IS_MOB(ch))
    learned = 75;
  else if(!(learned = has_skill(ch, SKILL_VITAL_STRIKE))) {
    send_to_char("You'd cut yourself to ribbons just trying!\r\n", ch);
    return eFAILURE;
  }

  if(!(ch->fighting)) {
    send_to_char("But you aren't fighting anyone!\r\n", ch);
    return eFAILURE;
  }

  specialization = learned / 100;
  learned %= 100;

  chance = 70;
  chance += learned / 10;

  percent = number(1, 101);

  skill_increase_check(ch, SKILL_VITAL_STRIKE, learned, SKILL_INCREASE_EASY);
  
  if(percent > chance) {
    act("$n starts jabbing $s weapons around $mself and almost chops off $s pinkie finger."
         , ch, 0, 0, TO_ROOM, NOTVICT);
    send_to_char("You try to begin the vital strike technique and slice off your own pinkie finger!\r\n", ch);
  } 
  else {
    act("$n begins jabbing $s weapons with lethal accuracy and strength.", ch, 0, 0, TO_ROOM, NOTVICT);
    send_to_char("Your body begins to coil, the strength building inside of you, your mind\r\n"
                 "pinpointing vital and vulnerable areas....\r\n", ch);
    SET_BIT(ch->combat, COMBAT_VITAL_STRIKE);
  }   
   
  WAIT_STATE(ch, PULSE_VIOLENCE);

  // learned should have max of 80 for mortal thieves
  // this means you can use it once per tick

  int length = 9 - learned / 10;
  if(length < 1)
    length = 1;

  af.type = SKILL_VITAL_STRIKE;
  af.duration  = length;
  af.modifier  = 0;
  af.location  = APPLY_NONE;
  af.bitvector = 0;
  affect_to_char(ch, &af);
  return eSUCCESS;
}


int do_deceit(struct char_data *ch, char *argument, int cmd)
{
  int learned, chance, specialization, percent;
  struct affected_type af;
  
  if(IS_MOB(ch) || GET_LEVEL(ch) >= ARCHANGEL)
    learned = 75;
  else if(!(learned = has_skill(ch, SKILL_DECEIT))) {
    send_to_char("You do not yet understand the workings of your marks.\r\n", ch);
    return eFAILURE;
  }   
      
  if(!IS_AFFECTED(ch, AFF_GROUP)) {
    send_to_char("You have no group to instruct.\r\n", ch);
    return eFAILURE;
  }   
      
  specialization = learned / 100;
  learned = learned % 100;
      
  chance = 75;
      
  // 101% is a complete failure
  percent = number(1, 101);
  if (percent > chance) {
     send_to_char("Guess your class just isn't up to the task.\r\n", ch);
     act ("$n tried to explain the weaknesses of other but you do not understand.", ch, 0, 0, TO_ROOM, 0);
  }
  else {
    act ("$n instructs $s group on the virtues of deceit.", ch, 0, 0, TO_ROOM, 0);
    send_to_char("Your instruction is well received and your pupils more able to exploit weakness.\r\n", ch);
    
    for(char_data * tmp_char = world[ch->in_room].people; tmp_char; tmp_char = tmp_char->next_in_room)
    { 
      if(tmp_char == ch)
        continue;
      if(!ARE_GROUPED(ch, tmp_char))
        continue;
      affect_from_char(tmp_char, SKILL_DECEIT);
      affect_from_char(tmp_char, SKILL_DECEIT);
      act ("$n lures your mind into the thought patterns of the morally corrupt.", ch, 0, tmp_char, TO_VICT, 0);
  
      af.type      = SKILL_DECEIT;
      af.duration  = 1 + learned / 10;
      af.modifier  = 1;
      af.location  = APPLY_MANA_REGEN;
      af.bitvector = 0;
      affect_to_char(tmp_char, &af);
      af.location  = APPLY_DAMROLL;
      affect_to_char(tmp_char, &af);
      af.location  = APPLY_HITROLL;
      affect_to_char(tmp_char, &af);
    }   
  }
    
  skill_increase_check(ch, SKILL_DECEIT, learned, SKILL_INCREASE_EASY);
  WAIT_STATE(ch, PULSE_VIOLENCE * 2);
  GET_MOVE(ch) /= 2;
  return eSUCCESS;
}
