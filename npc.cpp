
/* $Id: npc.cpp 535 2016-07-01 21:01:23Z Sausage $
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "npc.hpp"

#include "character.hpp"
#include "config.hpp"
#include "eodata.hpp"
#include "map.hpp"
#include "npc_data.hpp"
#include "packet.hpp"
#include "party.hpp"
#include "quest.hpp"
#include "pathfinder.hpp"
#include "timer.hpp"
#include "world.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/rpn.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static double speed_table[8] = {0.9, 0.6, 1.3, 1.9, 3.7, 7.5, 15.0, 0.0};


void NPC::SetSpeedTable(std::array<double, 7> speeds)
{
	for (std::size_t i = 0; i < 7; ++i)
	{
		if (speeds[i] != 0.0)
			speed_table[i] = speeds[i];
	}
}

NPC::NPC(Map *map, short id, unsigned char x, unsigned char y, unsigned char spawn_type, short spawn_time, unsigned char index, bool temporary,Character *owner)
{
	this->id = id;
	this->map = map;
	this->temporary = temporary;
	this->index = index;
	this->spawn_x = this->x = x;
	this->spawn_y = this->y = y;
	this->alive = false;
	this->attack = false;
	this->totaldamage = 0;

	if (spawn_type > 7)
	{
		spawn_type = 7;
	}

	this->spawn_type = spawn_type;
	this->spawn_time = spawn_time;
	this->walk_idle_for = 0;

	if (spawn_type == 7)
	{
		this->direction = static_cast<Direction>(spawn_time & 0x03);
		this->spawn_time = 0;
	}
	else
	{
		this->direction = DIRECTION_DOWN;
	}

	this->parent = 0;
	//new shit
	this->owner = owner;
	//this->pathfinder = 0;
    this->pathfinder = (this->map->world->config["PathFinding"] ? new PathFinder(this) : 0);
	this->given_path = false;
	this->marriage = 0;

	this->speech_type = "basic";
    this->speech = 0;
}

const NPC_Data& NPC::Data() const
{
	return *this->map->world->GetNpcData(this->id);
}

const ENF_Data& NPC::ENF() const
{
	return this->Data().ENF();
}

int NPC::CalculateDamage(NPC *target)
{
    int amount = util::rand(this->ENF().mindam, this->ENF().maxdam + static_cast<int>(this->map->world->config["NPCAdjustMaxDam"]));
	double rand = util::rand(0.0, 1.0);
	// Checks if target is facing you
	bool critical = std::abs(int(target->direction) - this->direction) != 2 || rand < static_cast<double>(this->map->world->config["CriticalRate"]);

	std::unordered_map<std::string, double> formula_vars;

	this->FormulaVars(formula_vars);
	target->FormulaVars(formula_vars, "target_");
	formula_vars["modifier"] = 1.0 / static_cast<double>(this->map->world->config["MobRate"]);
	formula_vars["damage"] = amount;
	formula_vars["critical"] = critical;

	amount = rpn_eval(rpn_parse(this->map->world->formulas_config["damage"]), formula_vars);
	double hit_rate = rpn_eval(rpn_parse(this->map->world->formulas_config["hit_rate"]), formula_vars);

	if (rand > hit_rate)
	{
		amount = 0;
	}

	amount = std::max(amount, 0);

	return amount;

}

bool  NPC::NoWall(unsigned char x,unsigned char y)
{
    return (this->owner && this == this->owner->pet && this->map->GetSpec(x,y) == Map_Tile::NPCBoundary);
}

std::pair<unsigned char,unsigned char> NPC::FindTargetBack(Character *char_target,NPC *npc_target)
{
    if(char_target && npc_target) return {0,0};

    unsigned char x = 0;
    unsigned char y = 0;

    if(char_target)
    {
        if(char_target->direction == DIRECTION_UP)
        {
            x = char_target->x;
            y = char_target->y + 1;
        }
        else if(char_target->direction == DIRECTION_DOWN)
        {
            x = char_target->x;
            y = char_target->y - 1;
        }
        else if(char_target->direction == DIRECTION_LEFT)
        {
            x = char_target->x +1;
            y = char_target->y;
        }
        else if(char_target->direction == DIRECTION_RIGHT)
        {
            x = char_target->x - 1;
            y = char_target->y;
        }
    }
    else if(npc_target)
    {
        if(npc_target->direction == DIRECTION_UP)
        {
            x = npc_target->x;
            y = npc_target->y + 1;
        }
        else if(char_target->direction == DIRECTION_DOWN)
        {
            x = npc_target->x;
            y = npc_target->y - 1;
        }
        else if(char_target->direction == DIRECTION_LEFT)
        {
            x = npc_target->x + 1;
            y = npc_target->y;
        }
        else if(char_target->direction == DIRECTION_RIGHT)
        {
            x = npc_target->x - 1;
            y = npc_target->y;
        }
    }
    return {x,y};
}

bool NPC::HandleSpeech(std::string type,Character *character)
{
   // std::cout<<Timer::GetTime()<<std::endl;
    if(!this->speech) return false;

    if(this->speech->basic_messages.empty() && this->speech->combat_messages.empty()
    && this->speech->death_messages.empty() && this->speech->character_inrange_messages.empty()) return false;

    if(this->speech->timer < Timer::GetTime())
    {
        bool proceed = false;
        UTIL_FOREACH(this->map->characters,check_char)
        {
            if(check_char->InRange(this))
            {
                proceed = true;
                break;
            }
        }

        if(!proceed) return false;

        std::string msg = "";

        if(type == "basic")
        {
            if(!this->speech->basic_messages.empty())
            msg = this->speech->basic_messages[util::rand(0,this->speech->basic_messages.size()-1)];
        }
        else if(type == "combat")
        {
            if(!this->speech->combat_messages.empty())
            msg = this->speech->combat_messages[util::rand(0,this->speech->combat_messages.size()-1)];
        }
        else if(type == "death")
        {
            if(!this->speech->death_messages.empty())
            msg = this->speech->death_messages[util::rand(0,this->speech->death_messages.size()-1)];
        }
        else if(type == "character_inrange")
        {
            if(!this->speech->character_inrange_messages.empty())
            msg = this->speech->character_inrange_messages[util::rand(0,this->speech->character_inrange_messages.size()-1)];
        }
        else msg = "";

        if(msg.empty()) return false;

        else
        {
            this->speech->timer = Timer::GetTime() + static_cast<double>(util::rand(static_cast<int>(this->map->world->npc_speech_config["SpeechIntervalMin"]),static_cast<int>(this->map->world->npc_speech_config["SpeechIntervalMax"])));

            if(character)
            {
                msg = util::replace_string(msg,"[CN]",character->real_name);
                msg = util::replace_string(msg,"[CT]",(character->title.empty() ? "Untitled" : character->title));

                ECF_Data clas_data = this->map->world->ecf->Get(character->clas);
                msg = util::replace_string(msg,"[CC]",(!clas_data ? "Unknown Class" : clas_data.name));
                msg = util::replace_string(msg,"[CG]",(!character->guild ? "Non Guild Member" : character->guild->name));
            }
            this->map->Msg(this,msg);
            return true;
        }
    }
    return false;
}

void NPC::Spawn(NPC *parent)
{
	if (this->alive)
		return;

    UTIL_FOREACH(this->map->world->npc_speeches,speech)
    {
        if(!this->speech && this->id == speech->id)
        {
            if(speech->basic_messages.empty() && speech->combat_messages.empty()
            && speech->death_messages.empty() && speech->character_inrange_messages.empty())continue;

            this->speech = speech;

            break;
        }
    }

	if (this->ENF().boss && !parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().child)
			{
				npc->Spawn(this);
			}
		}
	}

	if (parent)
	{
		this->parent = parent;
	}

	if (this->spawn_type < 7)
	{
		bool found = false;
		for (int i = 0; i < 200; ++i)
		{
			if (this->temporary && i == 0)
			{
				this->x = this->spawn_x;
				this->y = this->spawn_y;
			}
			else
			{
				this->x = util::rand(this->spawn_x-2, this->spawn_x+2);
				this->y = util::rand(this->spawn_y-2, this->spawn_y+2);
			}

			if (this->map->Walkable(this->x, this->y, true) && (i > 100 || !this->map->Occupied(this->x, this->y, Map::NPCOnly)))
			{
				this->direction = static_cast<Direction>(util::rand(0,3));
				found = true;
				break;
			}
		}

		if (!found)
		{
			Console::Wrn("An NPC on map %i at %i,%i is being placed by linear scan of spawn area (%s)", this->map->id, this->spawn_x, this->spawn_y, this->map->world->enf->Get(this->id).name.c_str());
			for (this->x = this->spawn_x-2; this->x <= spawn_x+2; ++this->x)
			{
				for (this->y = this->spawn_y-2; this->y <= this->spawn_y+2; ++this->y)
				{
					if (this->map->Walkable(this->x, this->y, true))
					{
						Console::Wrn("Placed at valid location: %i,%i", this->x, this->y);
						found = true;
						goto end_linear_scan;
					}
				}
			}
		}
		end_linear_scan:

		if (!found)
		{
			Console::Err("NPC couldn't spawn anywhere valid!");
		}
	}

	this->alive = true;
	this->hp = this->ENF().hp;
	this->last_act = Timer::GetTime();
	this->act_speed = speed_table[this->spawn_type];

	PacketBuilder builder(PACKET_APPEAR, PACKET_REPLY, 8);
	builder.AddChar(0);
	builder.AddByte(255);
	builder.AddChar(this->index);
	builder.AddShort(this->id);
	builder.AddChar(this->x);
	builder.AddChar(this->y);
	builder.AddChar(this->direction);

	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->InRange(this))
		{
			character->Send(builder);
		}
	}
}

void NPC::PetAct()
{
    int target_x = 0;
    int target_y = 0;
    int decision = 0;
    int distance = 0;
    bool walk = false;

    if(this->owner->pet_command == "None")
    {
        this->HandleSpeech("basic");
        target_x = this->FindTargetBack(this->owner).first;
        target_y = this->FindTargetBack(this->owner).second;

        decision = util::rand(0,100);
        distance = util::path_length(target_x, target_y, this->x, this->y);

        if(this->pathfinder && this->map->world->config["PathFinding"] && distance > 1)
        {
            // Console::Out("path %s", this->path.c_str());
            if(this->pathfinder->finish_x != target_x || this->pathfinder->finish_y != target_y)
            {
                this->pathfinder->finish_x = target_x;
                this->pathfinder->finish_y = target_y;
                this->pathfinder->path = "";
            }

            if(this->pathfinder->path.empty())
            {
                this->pathfinder->PathFinderMain(this->x,this->y,target_x,target_y);
            }
            //pmain should return path why its just if not else
            if(!this->pathfinder->path.empty())
            {
                this->pathfinder->ReducePath();
                walk = true;
             }
             else goto inner_else;
             //Console::Out("path %s", this->path.c_str());
        }
        else
        {
            inner_else:
            if(decision < 10)
            {
                this->direction = static_cast<Direction>(util::rand(0,3));
            }
            else if (decision >= 10 && decision < 35)
            {
                this->direction = this->owner->direction;
            }
            else if (decision >= 35 && decision < 95)
            {
                if(distance > 5)
                {
                    if(target_x < this->x)
                    this->direction = DIRECTION_LEFT;
                    if(target_y < this->y)
                    this->direction = DIRECTION_UP;
                    if(target_x > this->x)
                    this->direction = DIRECTION_RIGHT;
                    if(target_y > this->y)
                    this->direction = DIRECTION_DOWN;
                }
                else
                {
                    if(!this->HandleSpeech("character_inrange",this->owner))
                    this->HandleSpeech("basic");
                }
                if(decision >= 50)
                walk = true;
            }
            else if (decision >= 95)
		    walk = false;
        }

        UTIL_FOREACH(this->map->npcs, npc)
		{
		    if(!npc->alive || npc == this) continue;

		    target_x = npc->x;
            target_y = npc->y;
            distance = util::path_length(target_x, target_y, this->x, this->y);

		    if(distance > 0 && distance <= 5 && (npc->ENF().type == ENF::Aggressive || npc->ENF().type == ENF::Passive))
            {
                this->owner->pet_command = "Attack";
                //Console::Out("attack %i",  npc->id);
                break;
            }
		}

        int closest_distance = 11;
        std::shared_ptr<Map_Item> it = 0;
		UTIL_FOREACH(this->map->items, item)
		{
		    target_x = item->x;
            target_y = item->y;
            distance = util::path_length(target_x, target_y, this->x, this->y);

            if(distance < closest_distance)
		    {
		        closest_distance = distance;
		        it = item;
		    }
		}
		if(it)
        {
            target_x = it->x;
            target_y = it->y;
            distance = util::path_length(target_x, target_y, this->x, this->y);

            int taken = this->owner->CanHoldItem(it->id, it->amount);

            if (distance < 1 && taken > 0)
            {
                this->owner->AddItem(it->id, taken);

                PacketBuilder reply(PACKET_ITEM, PACKET_GET, 9);
                reply.AddShort(it->uid);
                reply.AddShort(it->id);
                reply.AddThree(taken);
                reply.AddChar(this->owner->weight);
                reply.AddChar(this->owner->maxweight);
                this->owner->Send(reply);
                this->owner->map->DelSomeItem(it->uid, taken, this->owner);
            }
            else
            {
                walk = true;
                if(target_x < this->x)
                this->direction = DIRECTION_LEFT;
                if(target_y < this->y)
                this->direction = DIRECTION_UP;
                if(target_x > this->x)
                this->direction = DIRECTION_RIGHT;
                if(target_y > this->y)
                this->direction = DIRECTION_DOWN;
            }
        }
		if(walk)
        {
		    if(this->map->Walk(this,this->direction) == Map::WalkFail)
            {
                this->map->Walk(this,static_cast<Direction>(util::rand(0,3)));
                this->pathfinder->path = "";
            }
        }
        else
        {
            this->walk_idle_for = util::rand(1,4);
        }
    }
    else if(this->owner->pet_command == "Attack")
    {
        NPC *pet_target = 0;
        decision = util::rand(0,10);
        distance = util::path_length(this->owner->x, this->owner->y, this->x, this->y);

        if(distance >= 12)
        {
            /*if(this->map->world->config["PathFinding"])
            {
                this->owner->pet_command = "None";
                goto find_owner;
            }
            else*/
            {
                this->owner->Warp(this->owner->map->id,this->owner->x,this->owner->y,WARP_ANIMATION_NONE);
                return;
            }
        }

        if(decision == 0)
        {
            if (this->Walk(static_cast<Direction>(util::rand(0,3))) == Map::WalkFail)
            this->Walk(static_cast<Direction>(util::rand(0,3)));
        }
        else if (decision >= 1 )
        {
            int closest_distance = 10;

            UTIL_FOREACH(this->map->npcs, npc)
		    {
		        if(!npc->alive || npc == this) continue;

		        distance = util::path_length(npc->x, npc->y, this->x, this->y);

                if(distance > 0 && distance <= closest_distance && (npc->ENF().type == ENF::Aggressive || npc->ENF().type == ENF::Passive))
                {
                    closest_distance = distance;

                    if(closest_distance == 0) break;

                    pet_target = npc;
                }
		    }

            if(pet_target)
            {
                //Console::Out("twat %i" , pet_target->id);
                int xdiff = this->x - pet_target->x;
		        int ydiff = this->y - pet_target->y;
		        int absxdiff = std::abs(xdiff);
		        int absydiff = std::abs(ydiff);

		        if ((absxdiff == 1 && absydiff == 0) || (absxdiff == 0 && absydiff == 1) || (absxdiff == 0 && absydiff == 0))
		        {
		            if (std::abs(xdiff) > std::abs(ydiff))
                    {
                        if (xdiff < 0)
                        {
                            this->direction = DIRECTION_RIGHT;
                        }
                        else
                        {
                            this->direction = DIRECTION_LEFT;
                        }
                    }
                    else
                    {
                        if (ydiff < 0)
                        {
                            this->direction = DIRECTION_DOWN;
                        }
                        else
                        {
                            this->direction = DIRECTION_UP;
                        }
                    }

                    UTIL_FOREACH(this->map->characters, character)
                    {
                        PacketBuilder builder(PACKET_NPC, PACKET_PLAYER);
                        builder.AddByte(255);
                        builder.AddChar(this->index);
                        builder.AddChar(1 + (character->hp == 0));
                        builder.AddChar(this->direction);
                        builder.AddShort(0);//player id
                        builder.AddThree(0);
                        builder.AddThree(int(double(character->hp) / double(character->maxhp) * 100.0));
                        builder.AddByte(255);
                        builder.AddByte(255);

                        if(character->InRange(this))
                        character->Send(builder);
                    }

                    pet_target->Damage(this->owner, this->CalculateDamage(pet_target));
                    return;
                }
                else if (absxdiff > absydiff)
                {
                    if (xdiff < 0)
                    {
                        this->direction = DIRECTION_RIGHT;
                    }
                    else
                    {
                        this->direction = DIRECTION_LEFT;
                    }
                }
                else
                {
                    if (ydiff < 0)
                    {
                        this->direction = DIRECTION_DOWN;
                    }
                    else
                    {
                        this->direction = DIRECTION_UP;
                    }
                }
                if (this->Walk(this->direction) == Map::WalkFail)
                this->Walk(static_cast<Direction>(util::rand(0,3)));
                //this->owner->pet_command = "None";
            }
            if(!pet_target)
            this->owner->pet_command = "None";
        }
    }
}

void NPC::Act()
{
	// Needed for the server startup spawn to work properly
	if (this->ENF().child && !this->parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().boss)
			{
				this->parent = npc;
				break;
			}
		}
	}

	bool pfind = this->map->world->config["PathFinding"];
    if(this->pathfinder && !pfind)
        this->pathfinder = 0;

    if(!this->pathfinder && pfind)
        this->pathfinder = new PathFinder(this);

    //Console::Out("b %i",this->id);
	this->last_act += double(util::rand(int(this->act_speed * 750.0), int(this->act_speed * 1250.0))) / 1000.0;

	if (this->spawn_type == 7)
	{
		return;
	}

    //Console::Out("a");
    if(this->owner && this->owner->pet == this)
    {
        this->PetAct();
        return;
    }

	Character *attacker = 0;
	NPC *npc_attacker = 0;
	unsigned char attacker_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);
	unsigned short attacker_damage = 0;

    Character *speaker = 0;
    int speaker_distance = 3;
    int closest_speaker_distance = speaker_distance;

    UTIL_FOREACH(this->map->characters,character)
    {
        speaker_distance = util::path_length(character->x, character->y, this->x, this->y);
        if(speaker_distance <= closest_speaker_distance)
        {
            speaker = character;
            closest_speaker_distance = speaker_distance;
            //Console::Out("id for speech %i",this->id);
        }
    }

    if(speaker)
    {
        if(!this->HandleSpeech("character_inrange",speaker))
        this->HandleSpeech("basic");
    }
    else this->HandleSpeech("basic");

	if (this->ENF().type == ENF::Passive || this->ENF().type == ENF::Aggressive)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
			{
				continue;
			}

			int distance = util::path_length(opponent->attacker->x, opponent->attacker->y, this->x, this->y);

			if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
			{
				attacker = opponent->attacker;
				attacker_damage = opponent->damage;
				attacker_distance = distance;

				if(attacker->pet)npc_attacker = attacker->pet;
			}
		}

		if (this->parent)
		{
			UTIL_FOREACH_CREF(this->parent->damagelist, opponent)
			{
				if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
				{
					continue;
				}
				int distance = util::path_length(opponent->attacker->x, opponent->attacker->y, this->x, this->y);

				if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
				{
					attacker = opponent->attacker;
					attacker_damage = opponent->damage;
					attacker_distance = distance;

					if(attacker->pet)npc_attacker = attacker->pet;
				}
			}
		}
	}

    unsigned char closest_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);
    unsigned char char_distance = closest_distance;
    unsigned char npc_distance = closest_distance;

	if (this->ENF().type == ENF::Aggressive || (this->parent && attacker))
	{
		Character *closest = 0;
		NPC *npc_closest = 0;

		if (attacker)
		{
			closest = attacker;
			closest_distance = std::min(closest_distance, attacker_distance);

            if(attacker->pet)npc_closest = attacker->pet;
		}

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->IsHideNpc() || !character->CanInteractCombat())
				continue;

			char_distance = util::path_length(character->x, character->y, this->x, this->y);

			if (char_distance < closest_distance)
			{
				closest = character;
				closest_distance = char_distance;
			}
		}

		if (closest)
		{
			attacker = closest;
		}

		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (!npc->owner)continue;

			npc_distance = util::path_length(npc->x, npc->y, this->x, this->y);

			if (npc_distance > 0 && npc_distance < closest_distance)
			{
				npc_closest = npc;
				closest_distance = npc_distance;
			}
		}

		if (npc_closest)
		{
			npc_attacker = npc_closest;
		}
	}

    bool found_path = false;
    bool attack_npc = false;
    int xdiff = 0;
    int ydiff = 0;

	if (npc_attacker || attacker)
	{
	    int attacker_x = 0;
        int attacker_y = 0;

        if (attacker)
        {
            attacker_x = attacker->x;
            attacker_y = attacker->y;
        }

	    if(npc_attacker)
        {
            if(attacker)
            char_distance = util::path_length(attacker->x, attacker->y, this->x, this->y);
            else
            char_distance = 11;

            npc_distance = util::path_length(npc_attacker->x, npc_attacker->y, this->x, this->y);

            if(!attacker || npc_distance <= char_distance)
            {
                int decision = 0;

                if(npc_distance == char_distance)
                decision = util::rand(0,1);

                if(decision == 0)
                {
                    attack_npc = true;
                    attacker_x = npc_attacker->x;
                    attacker_y = npc_attacker->y;
                }
            }
        }

        if(this->pathfinder)
        {
            //Console::Out("L");
            if(this->pathfinder->finish_x != attacker_x || this->pathfinder->finish_y != attacker_y)
            {
                this->pathfinder->finish_x = attacker_x;
                this->pathfinder->finish_y = attacker_y;
                this->pathfinder->path = "";
            }

            if(this->pathfinder->path.empty())
            {
                int search_radius = static_cast<int>(this->map->world->config["PathFindingRadius"]);

                if((npc_attacker && attack_npc && npc_distance > 1 && npc_distance <= search_radius)
                ||(attacker && !attack_npc && char_distance > 1 && char_distance <= search_radius))
                {
                    this->pathfinder->PathFinderMain(this->x,this->y,attacker_x,attacker_y);
                }
            }
            //pmain should return path why its just if not els
            if(!this->pathfinder->path.empty())
            {
                this->pathfinder->ReducePath();
                found_path = true;
            }
            else
            {
                this->pathfinder->path = "";
            }
        }

        xdiff = this->x - attacker_x;
        ydiff = this->y - attacker_y;
        int absxdiff = std::abs(xdiff);
        int absydiff = std::abs(ydiff);

        if ((absxdiff == 1 && absydiff == 0) || (absxdiff == 0 && absydiff == 1) || (absxdiff == 0 && absydiff == 0))
        {
            if(!attack_npc)
            {
                this->Attack(attacker);
            }
            else
            {
                if(!found_path)
                {
                    if (std::abs(xdiff) > std::abs(ydiff))
                    {
                        if (xdiff < 0)
                        {
                            this->direction = DIRECTION_RIGHT;
                        }
                        else
                        {
                            this->direction = DIRECTION_LEFT;
                        }
                    }
                    else
                    {
                        if (ydiff < 0)
                        {
                            this->direction = DIRECTION_DOWN;
                        }
                        else
                        {
                            this->direction = DIRECTION_UP;
                        }
                    }
                }

                if(npc_attacker->owner->pet_command == "OpenLocker")
                {
                    npc_attacker->owner->pet_command = "None";
                }

                UTIL_FOREACH(this->map->characters, character)
                {
                    PacketBuilder builder(PACKET_NPC, PACKET_PLAYER);
                    builder.AddByte(255);
                    builder.AddChar(this->index);
                    builder.AddChar(1 + (character->hp == 0));
                    builder.AddChar(this->direction);
                    builder.AddShort(0);//player id
                    builder.AddThree(0);
                    builder.AddThree(int(double(character->hp) / double(character->maxhp) * 100.0));
                    builder.AddByte(255);
                    builder.AddByte(255);

                    if(character->InRange(this))
                    character->Send(builder);
                }
                npc_attacker->Damage(npc_attacker->owner, this->CalculateDamage(npc_attacker));
            }
            return;
        }
        else if (absxdiff > absydiff)
        {
            if(!found_path)
            {
                if (xdiff < 0)
                {
                    this->direction = DIRECTION_RIGHT;
                }
                else
                {
                    this->direction = DIRECTION_LEFT;
                }
            }
        }
        else
        {
            if(!found_path)
            {
                if (ydiff < 0)
                {
                    this->direction = DIRECTION_DOWN;
                }
                else
                {
                    this->direction = DIRECTION_UP;
                }
            }
        }

		if (this->Walk(this->direction) == Map::WalkFail)
		{
			if (this->direction == DIRECTION_UP || this->direction == DIRECTION_DOWN)
			{
				if (xdiff < 0)
				{
					this->direction = DIRECTION_RIGHT;
				}
				else
				{
					this->direction = DIRECTION_LEFT;
				}
			}

			if (this->Walk(static_cast<Direction>(this->direction)) == Map::WalkFail)
			{
				this->Walk(static_cast<Direction>(util::rand(0,3)));
			}
			if(this->pathfinder)
                this->pathfinder->path = "";
		}
	}
	else
	{
		// Random walking

		int act;
		if (this->walk_idle_for == 0)
		{
			act = util::rand(1,10);
		}
		else
		{
			--this->walk_idle_for;
			act = 11;
		}

		if (act >= 1 && act <= 6) // 60% chance walk foward
		{
			this->Walk(this->direction);
		}

		if (act >= 7 && act <= 9) // 30% change direction
		{
			this->Walk(static_cast<Direction>(util::rand(0,3)));
		}

		if (act == 10) // 10% take a break
		{
			this->walk_idle_for = util::rand(1,4);
		}
		if(this->pathfinder)
            this->pathfinder->path = "";
	}
}

bool NPC::Walk(Direction direction)
{
	return this->map->Walk(this, direction);
}

void NPC::Damage(Character *from, int amount, int spell_id)
{
	int limitamount = std::min(this->hp, amount);

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	if (this->ENF().type == ENF::Passive || this->ENF().type == ENF::Aggressive || (this->owner && this->owner->pet))
	{
		this->hp -= amount;
	}
	else
	{
		this->hp = 0;
		amount = 0;
	}

	if (this->totaldamage + limitamount > this->totaldamage)
		this->totaldamage += limitamount;

	std::unique_ptr<NPC_Opponent> opponent(new NPC_Opponent);
	bool found = false;

	UTIL_FOREACH_CREF(this->damagelist, checkopp)
	{
		if (checkopp->attacker == from)
		{
			found = true;

			if (checkopp->damage + limitamount > checkopp->damage)
				checkopp->damage += limitamount;

			checkopp->last_hit = Timer::GetTime();
		}
	}

	if (!found)
	{
        opponent->attacker = from;
        opponent->attacker->unregister_npc.push_back(this);
        opponent->damage = limitamount;
		opponent->last_hit = Timer::GetTime();
		this->damagelist.emplace_back(std::move(opponent));
	}

	if (this->hp > 0)
	{
		PacketBuilder builder(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_REPLY, 14);

		if (spell_id != -1)
			builder.AddShort(spell_id);

		builder.AddShort(from->PlayerID());
		builder.AddChar(from->direction);
		builder.AddShort(this->index);
		builder.AddThree(amount);
		builder.AddShort(util::clamp<int>(double(this->hp) / double(this->ENF().hp) * 100.0, 0, 100));

		if (spell_id != -1)
			builder.AddShort(from->tp);
		else
			builder.AddChar(1); // ?

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->InRange(this))
			{
				character->Send(builder);
			}
		}
	}
	else
	{
        if(this->speech && util::rand(0,4) == 4)
        {
            this->speech->timer = 0.0;
            this->HandleSpeech("death");
        }

	    if(this->owner)
        this->owner->RemovePet();
        else
        {
		    this->Killed(from, amount, spell_id);
		    // *this may not be valid here
		    return;
        }
	}
}

void NPC::RemoveFromView(Character *target)
{
	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 7);
	builder.AddChar(this->index);
	if (target->x > 200 && target->y > 200)
	{
		builder.AddChar(0); // x
		builder.AddChar(0); // y
	}
	else
	{
		builder.AddChar(252); // x
		builder.AddChar(252); // y
	}
	builder.AddChar(0); // direction
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddByte(255);

	PacketBuilder builder2(PACKET_NPC, PACKET_SPEC, 5);
	builder2.AddShort(0); // killer pid
	builder2.AddChar(0); // killer direction
	builder2.AddShort(this->index);
/*
	builder2.AddShort(0); // dropped item uid
	builder2.AddShort(0); // dropped item id
	builder2.AddChar(this->x);
	builder2.AddChar(this->y);
	builder2.AddInt(0); // dropped item amount
	builder2.AddThree(0); // damage
*/

	target->Send(builder);
	target->Send(builder2);
}

void NPC::Killed(Character *from, int amount, int spell_id)
{
	double droprate = this->map->world->config["DropRate"];
	double exprate = this->map->world->config["ExpRate"];
	int sharemode = this->map->world->config["ShareMode"];
	int partysharemode = this->map->world->config["PartyShareMode"];
	int dropratemode = this->map->world->config["DropRateMode"];
	std::set<Party *> parties;

	int most_damage_counter = 0;
	Character *most_damage = nullptr;
	NPC_Drop *drop = nullptr;

	this->alive = false;

	this->dead_since = int(Timer::GetTime());

	if (dropratemode == 1)
	{
		std::vector<NPC_Drop *> drops;

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drops.push_back(checkdrop.get());
			}
		}

		if (drops.size() > 0)
		{
			drop = drops[util::rand(0, drops.size()-1)];
		}
	}
	else if (dropratemode == 2)
	{
		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}
	else if (dropratemode == 3)
	{
		double roll = util::rand(0.0, this->Data().drops_chance_total);

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (roll >= checkdrop->chance_offset && roll < checkdrop->chance_offset+checkdrop->chance)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}

	if (sharemode == 1)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->damage > most_damage_counter)
			{
				most_damage_counter = opponent->damage;
				most_damage = opponent->attacker;
			}
		}
	}

	int dropuid = 0;
	int dropid = 0;
	int dropamount = 0;
	Character* drop_winner = nullptr;

	if (drop)
	{
		dropid = drop->id;
		dropamount = std::min<int>(util::rand(drop->min, drop->max), this->map->world->config["MaxItem"]);

		if (dropid <= 0 || static_cast<std::size_t>(dropid) >= this->map->world->eif->data.size() || dropamount <= 0)
			goto abort_drop;

		dropuid = this->map->GenerateItemID();

		std::shared_ptr<Map_Item> newitem(std::make_shared<Map_Item>(dropuid, dropid, dropamount, this->x, this->y, from->PlayerID(), Timer::GetTime() + static_cast<int>(this->map->world->config["ProtectNPCDrop"])));
		this->map->items.push_back(newitem);

		// Selects a random number between 0 and maxhp, and decides the winner based on that
		switch (sharemode)
		{
			case 0:
				drop_winner = from;
				break;

			case 1:
				drop_winner = most_damage;
				break;

			case 2:
			{
				int rewarded_hp = util::rand(0, this->totaldamage - 1);
				int count_hp = 0;
				UTIL_FOREACH_CREF(this->damagelist, opponent)
				{
					if (opponent->attacker->InRange(this))
					{
						if (rewarded_hp >= count_hp && rewarded_hp < opponent->damage)
						{
							drop_winner = opponent->attacker;
							break;
						}

						count_hp += opponent->damage;
					}
				}
			}
				break;

			case 3:
			{
				int rand = util::rand(0, this->damagelist.size() - 1);
				int i = 0;
				UTIL_FOREACH_CREF(this->damagelist, opponent)
				{
					if (opponent->attacker->InRange(this))
					{
						if (rand == i++)
						{
							drop_winner = opponent->attacker;
							break;
						}
					}
				}
			}
				break;
		}
	}
	abort_drop:

	if (drop_winner)
		this->map->items.back()->owner = drop_winner->PlayerID();

	UTIL_FOREACH(this->map->characters, character)
	{
		std::list<std::unique_ptr<NPC_Opponent>>::iterator findopp = this->damagelist.begin();
		for (; findopp != this->damagelist.end() && (*findopp)->attacker != character; ++findopp); // no loop body

		if (findopp != this->damagelist.end() || character->InRange(this))
		{
			bool level_up = false;

			PacketBuilder builder(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_SPEC, 26);

			if (this->ENF().exp != 0)
			{
				if (findopp != this->damagelist.end())
				{
					int reward;
					switch (sharemode)
					{
						case 0:
							if (character == from)
							{
								reward = int(std::ceil(double(this->ENF().exp) * exprate));

								if (reward > 0)
								{
									if (partysharemode)
									{
										if (character->party)
										{
											character->party->ShareEXP(reward, partysharemode, this->map);
										}
										else
										{
											character->exp += reward;
										}
									}
									else
									{
										character->exp += reward;
									}
								}
							}
							break;

						case 1:
							if (character == most_damage)
							{
								reward = int(std::ceil(double(this->ENF().exp) * exprate));

								if (reward > 0)
								{
									if (partysharemode)
									{
										if (character->party)
										{
											character->party->ShareEXP(reward, partysharemode, this->map);
										}
										else
										{
											character->exp += reward;
										}
									}
									else
									{
										character->exp += reward;
									}
								}
							}
							break;

						case 2:
							reward = int(std::ceil(double(this->ENF().exp) * exprate * (double((*findopp)->damage) / double(this->totaldamage))));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->temp_expsum += reward;
										parties.insert(character->party);
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
							break;

						case 3:
							reward = int(std::ceil(double(this->ENF().exp) * exprate * (double(this->damagelist.size()) / 1.0)));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->temp_expsum += reward;
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
							break;
					}

                    if(character == from->side_kick)
                    {
                        reward = reward / 2;
                        from->exp = std::min(from->exp + reward, static_cast<int>(this->map->world->config["MaxExp"]));
					    character->exp = std::min(character->exp - reward, static_cast<int>(this->map->world->config["MaxExp"]));
                    }
                    else
                    {
                        character->exp = std::min(character->exp, static_cast<int>(this->map->world->config["MaxExp"]));
                    }

					while (character->level < static_cast<int>(this->map->world->config["MaxLevel"]) && character->exp >= this->map->world->exp_table[character->level+1])
					{
						level_up = true;
						++character->level;
						character->statpoints += static_cast<int>(this->map->world->config["StatPerLevel"]);
						character->skillpoints += static_cast<int>(this->map->world->config["SkillPerLevel"]);
						character->CalculateStats();
					}

					if (level_up)
					{
						builder.SetID(spell_id == -1 ? PACKET_NPC : PACKET_CAST, PACKET_ACCEPT);
						builder.ReserveMore(33);
					}
				}
			}

			if (spell_id != -1)
				builder.AddShort(spell_id);

			builder.AddShort(drop_winner ? drop_winner->PlayerID() : from->PlayerID());
			builder.AddChar(drop_winner ? drop_winner->direction : from->direction);
			builder.AddShort(this->index);
			builder.AddShort(dropuid);
			builder.AddShort(dropid);
			builder.AddChar(this->x);
			builder.AddChar(this->y);
			builder.AddInt(dropamount);
			builder.AddThree(amount);

			if (spell_id != -1)
				builder.AddShort(from->tp);

			if ((sharemode == 0 && character == from) || (sharemode != 0 && findopp != this->damagelist.end()))
			{
				builder.AddInt(character->exp);
			}

			if (level_up)
			{
				builder.AddChar(character->level);
				builder.AddShort(character->statpoints);
				builder.AddShort(character->skillpoints);
				builder.AddShort(character->maxhp);
				builder.AddShort(character->maxtp);
				builder.AddShort(character->maxsp);
			}

			character->Send(builder);
		}
	}

	UTIL_FOREACH(parties, party)
	{
		party->ShareEXP(party->temp_expsum, partysharemode, this->map);
		party->temp_expsum = 0;
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	short childid = -1;

	if (this->ENF().boss)
	{
		std::vector<NPC*> child_npcs;

		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->ENF().child && !npc->ENF().boss && npc->alive)
			{
				child_npcs.push_back(npc);
			}
		}

		UTIL_FOREACH(child_npcs, npc)
		{
			if (!npc->temporary && (childid == -1 || childid == npc->id))
			{
				npc->Die(false);
				childid = npc->id;
			}
			else
			{
				npc->Die(true);
			}
		}
	}

	if (childid != -1)
	{
		PacketBuilder builder(PACKET_NPC, PACKET_JUNK, 2);
		builder.AddShort(childid);

		UTIL_FOREACH(this->map->characters, character)
		{
			character->Send(builder);
		}
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end()
		);
	}

	UTIL_FOREACH(from->quests, q)
	{
		if (!q.second || q.second->GetQuest()->Disabled())
			continue;

		q.second->KilledNPC(this->ENF().id);
	}

	if (this->temporary)
	{
		delete this;
		return;
	}
}

void NPC::Die(bool show)
{
	if (!this->alive)
		return;

	this->alive = false;
	this->parent = 0;
	this->dead_since = int(Timer::GetTime());

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	if (show)
	{
		PacketBuilder builder(PACKET_NPC, PACKET_SPEC, 18);
		builder.AddShort(0); // killer pid
		builder.AddChar(0); // killer direction
		builder.AddShort(this->index);
		builder.AddShort(0); // dropped item uid
		builder.AddShort(0); // dropped item id
		builder.AddChar(this->x);
		builder.AddChar(this->y);
		builder.AddInt(0); // dropped item amount
		builder.AddThree(this->hp); // damage

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->InRange(this))
			{
				character->Send(builder);
			}
		}
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end()
		);

		delete this;
	}
}

void NPC::Attack(Character *target)
{
	int amount = util::rand(this->ENF().mindam, this->ENF().maxdam + static_cast<int>(this->map->world->config["NPCAdjustMaxDam"]));
	double rand = util::rand(0.0, 1.0);
	// Checks if target is facing you
	bool critical = std::abs(int(target->direction) - this->direction) != 2 || rand < static_cast<double>(this->map->world->config["CriticalRate"]);

	std::unordered_map<std::string, double> formula_vars;

	this->FormulaVars(formula_vars);
	target->FormulaVars(formula_vars, "target_");
	formula_vars["modifier"] = 1.0 / static_cast<double>(this->map->world->config["MobRate"]);
	formula_vars["damage"] = amount;
	formula_vars["critical"] = critical;

	amount = rpn_eval(rpn_parse(this->map->world->formulas_config["damage"]), formula_vars);
	double hit_rate = rpn_eval(rpn_parse(this->map->world->formulas_config["hit_rate"]), formula_vars);

	if (rand > hit_rate)
	{
		amount = 0;
	}

	amount = std::max(amount, 0);

	int limitamount = std::min(amount, int(target->hp));

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	target->hp -= limitamount;
	if (target->party)
	{
		target->party->UpdateHP(target);
	}

	int xdiff = this->x - target->x;
	int ydiff = this->y - target->y;

	if (std::abs(xdiff) > std::abs(ydiff))
	{
		if (xdiff < 0)
		{
			this->direction = DIRECTION_RIGHT;
		}
		else
		{
			this->direction = DIRECTION_LEFT;
		}
	}
	else
	{
		if (ydiff < 0)
		{
			this->direction = DIRECTION_DOWN;
		}
		else
		{
			this->direction = DIRECTION_UP;
		}
	}

	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 18);
	builder.AddByte(255);
	builder.AddChar(this->index);
	builder.AddChar(1 + (target->hp == 0));
	builder.AddChar(this->direction);
	if(!target->is_side_kick)
	builder.AddShort(target->PlayerID());
	else
    builder.AddShort(target->PlayerID()+1000);
	builder.AddThree(amount);
	builder.AddThree(util::clamp<int>(double(target->hp) / double(target->maxhp) * 100.0, 0, 100));
	builder.AddByte(255);
	builder.AddByte(255);

	UTIL_FOREACH(this->map->characters, character)
	{
		if (character == target || !character->InRange(target))
		{
			continue;
		}

		character->Send(builder);
	}

    if(target->pet_command == "OpenLocker")
    {
        target->pet_command  = "None";
    }

    if(this->speech && util::rand(0,4) == 4)
    {
         if(util::rand(0,4) == 4)
        this->speech->timer = 0.0;

        this->HandleSpeech("combat",target);
    }

	if (target->hp == 0)
	{
	    if(!target->is_side_kick)
		target->DeathRespawn();
		else
        {
            target->hp = int(target->maxhp * static_cast<double>(target->world->config["DeathRecover"]) / 100.0);

	        if (target->world->config["Deadly"])
	        {
		        target->DropAll(nullptr);
	        }

            Character *owner = 0;
            UTIL_FOREACH(this->map->world->characters,world_char)
            {
                if(!world_char->side_kick) continue;

                if(world_char->side_kick == target)
                {
                    owner = world_char;
                    break;
                }
            }

            if(!owner)
            {
                owner = target;
            }

	        target->nowhere = true;
	        target->map = owner->map;
	        target->mapid = owner->mapid;
            target->x = owner->x;
	        target->y = owner->y;
	        target->nowhere = false;
	        target->Save();

	        /*UTIL_FOREACH(this->map->world->characters,owner)
	        {
	            if(owner->side_kick && owner->side_kick == target)
                {
                    owner->side_kick = 0;
                    target = 0;
                    break;
                }
	        }*/
        }
	}

    if(target && !target->is_side_kick)
	{
	    builder.AddShort(target->hp);
	    builder.AddShort(target->tp);

	    target->Send(builder);
	}

	this->HandleSpeech("combat",target);
}

#define v(x) vars[prefix + #x] = x;
#define vv(x, n) vars[prefix + n] = x;
#define vd(x) vars[prefix + #x] = data.x;

void NPC::FormulaVars(std::unordered_map<std::string, double> &vars, std::string prefix)
{
	const ENF_Data& data = this->ENF();
	vv(1, "npc") v(hp) vv(data.hp, "maxhp")
	vd(mindam) vd(maxdam)
	vd(accuracy) vd(evade) vd(armor)
	v(x) v(y) v(direction) vv(map->id, "mapid")
}

#undef vd
#undef vv
#undef v

NPC::~NPC()
{
	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->npc == this)
		{
			character->npc = 0;
			character->npc_type = ENF::NPC;
		}
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}
}
