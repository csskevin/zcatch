/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
/* Modified by Teelevision for zCatch/TeeVi, see readme.txt and license.txt.                 */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
/*#include "gamemodes/dm.h"
#include "gamemodes/tdm.h"
#include "gamemodes/ctf.h"
#include "gamemodes/mod.h"*/
#include "gamemodes/zcatch.h"

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
	
	for(int i = 0; i < MAX_MUTES; i++)
		m_aMutes[i].m_aIP[0] = 0;
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, bool limitVictims, const bool *victims)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		bool someoneWasHit = false;
		for(int i = 0; i < Num; i++)
		{
			if(!limitVictims || victims[apEnts[i]->GetPlayer()->GetCID()])
			{
				vec2 Diff = apEnts[i]->m_Pos - Pos;
				vec2 ForceDir(0,1);
				float l = length(Diff);
				if(l)
					ForceDir = normalize(Diff);
				l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
				float Dmg = 6 * l;
				if((int)Dmg)
				{
					apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon);
					someoneWasHit = true;
				}
			}
		}
		// give the owner the ammo back if he hit someone else or himself (like rocketjump)
		if(someoneWasHit)
		{
			CCharacter *ownerChar = GetPlayerChar(Owner);
			if(ownerChar)
				ownerChar->GiveAmmo(Weapon, 1);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}


void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}


void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			//if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
			if(m_apPlayers[i] && ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS && m_apPlayers[ChatterClientID] && m_apPlayers[ChatterClientID]->m_SpecExplicit == m_apPlayers[i]->m_SpecExplicit)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;
	
	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_apPlayers[i]->SetTeam(m_apPlayers[i]->GetTeam()^1, false);
	}

	(void)m_pController->CheckTeamBalance();
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					/* zCatch - Allow voting from players in spectators (needed or the last 2 players ingame can kick the whole server),
					 * but deny votes from players who are explicit in spec
					*/
					if(!m_apPlayers[i] || m_apPlayers[i]->m_SpecExplicit || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}
	
	// info messages
	// execute if interval is given and message interval is due, respecting the pause
	if(Server()->GetInfoTextInterval() > 0
		&& ((Server()->Tick() % Server()->GetInfoTextInterval()) - Server()->GetInfoTextIntervalPause()) % Server()->GetInfoTextMsgInterval() == 0)
	{
		SendChat(-1, CGameContext::CHAT_ALL, Server()->GetNextInfoText().c_str());
	}

	// bot detection
	// it is based on the behaviour of some bots to shoot at a player's _exact_ position
	// check each player, check only if an admin is online
	if(g_Config.m_SvBotDetection && Server()->GetNumLoggedInAdmins())
	{
		char aBuf[128], bBuf[64];
		const vec2 *pos, *posVictim;
		float d, precision;
		CCharacter *ci, *cj;
		CPlayer *p;
		
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
		
			// abort if player is not ingame or already detected as a bot
			if(!(p = m_apPlayers[i]) || (p->m_IsAimBot  && p->m_IsAimBot+Server()->TickSpeed()*30 > Server()->Tick()) || !(ci = GetPlayerChar(i)))
				continue;
			
			// check against every other player
			for(int j = 0; j < MAX_CLIENTS; ++j)
			{

				if(j != i && (cj = GetPlayerChar(j)))
				{
					int indexAdd = 0;
					vec2 target(p->m_LatestActivity.m_TargetX, p->m_LatestActivity.m_TargetY);
					
					// fast aiming bot detection
					if(g_Config.m_SvBotDetection&BOT_DETECTION_FAST_AIM
						&& p->m_AimBotTargetSpeed > 300.0 // only fast movements
						&& (d = cj->HowCloseToXRecently(ci->m_Pos + target, posVictim, p->m_AimBotLastDetection)) < 16.0
						&& (precision = p->m_AimBotTargetSpeed * (256.0 - d * d)) >= 50000.0
						&& !( // don't detect same constellation twice
							ci->m_Pos == p->m_AimBotLastDetectionPos
							&& *posVictim == p->m_AimBotLastDetectionPosVictim
						)
					)//if
					{
						indexAdd = min(3, (int)(precision / 50000));
						p->m_AimBotLastDetectionPos = ci->m_Pos;
						// prepare console output
						str_format(bBuf, sizeof(bBuf), "precision=%d speed=%d distance=%d", (int)precision, (int)p->m_AimBotTargetSpeed, (int)d);
					}
					
					// follow bot detection
					else if(g_Config.m_SvBotDetection&BOT_DETECTION_FOLLOW
						&& cj->NetworkClipped(i) == 0 // needs to be in sight
						&& ci->AimedAtCharRecently(target, cj, pos, posVictim, p->m_AimBotLastDetection)
						&& !( // don't detect same constellation twice
							*pos == p->m_AimBotLastDetectionPos
							&& *posVictim == p->m_AimBotLastDetectionPosVictim
						)
						// don't detect horizontal dragging
						&& !(
							pos->y == p->m_AimBotLastDetectionPos.y
							&& posVictim->y == p->m_AimBotLastDetectionPosVictim.y
						)
					)//if
					{
						indexAdd = 1;
						p->m_AimBotLastDetectionPos = *pos;
						// prepare console output
						bBuf[0] = 0;
					}
					
					// detected
					if(indexAdd > 0)
					{
						p->m_AimBotLastDetection = Server()->Tick();
						p->m_AimBotLastDetectionPosVictim = *posVictim;
						p->m_AimBotIndex += indexAdd;
						p->m_AimBotRange = max(p->m_AimBotRange, (int)length(target));
						// log to console
						str_format(aBuf, sizeof(aBuf), "player=%d victim=%d index=%d %s", i, j, p->m_AimBotIndex, bBuf);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "botdetect", aBuf);
						// don't check other players
						break;
					}
					
				}
			}
			
			// check if threshold is exceeded
			if(p->m_AimBotIndex >= 5)
			{
				p->m_IsAimBot = Server()->Tick();
				// alert the admins
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "+++ '%s' (id=%d,range=%d) might be botting +++", Server()->ClientName(i), i, p->m_AimBotRange);
				for(int j = 0; j < MAX_CLIENTS; ++j)
					if(Server()->IsAuthed(j))
						SendChatTarget(j, aBuf);

				//Reset informations for better detection
				p->m_AimBotIndex = 0;
				p->m_AimBotRange = 0;
				p->m_AimBotLastDetection = 0;
				p->m_AimBotTargetSpeed = .0;
				p->m_CurrentTarget.x = 0;
				p->m_CurrentTarget.y = 0;
				p->m_LastTarget.x = 0;
				p->m_LastTarget.y = 0;
			}
			
			// reduce once every seconds (tolerance)
			if(((Server()->Tick() % Server()->TickSpeed()) == 0) && p->m_AimBotIndex)
			{
				if(!(--p->m_AimBotIndex))
				{
					p->m_AimBotRange = 0;
				}
			}
		}
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientEnter(int ClientID)
{
	CPlayer *p = m_apPlayers[ClientID];
	//world.insert_entity(&players[client_id]);
	p->Respawn();
	
	/* begin zCatch */
	CPlayer *leader = NULL;
	
	int NumReady = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(IsClientReady(i))
			NumReady++;
	}

	// sv_allow_join 1: Allow new players to join the game without need to wait for the next round
	if(g_Config.m_SvAllowJoin == 1)
	{
		p->m_SpecExplicit = (NumReady > 2);
		p->SetTeamDirect(p->m_SpecExplicit ? TEAM_SPECTATORS : m_pController->ClampTeam(1));
		SendBroadcast("You can join the game", ClientID);
	}
	// sv_allow_join 2: The player will join when the player with the most kills dies
	else if(g_Config.m_SvAllowJoin == 2)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			if(m_apPlayers[i] && ((leader && m_apPlayers[i]->m_zCatchNumKillsInARow > leader->m_zCatchNumKillsInARow) || (!leader && m_apPlayers[i]->m_zCatchNumKillsInARow)))
				leader = m_apPlayers[i];
		if(leader)
			leader->AddZCatchVictim(ClientID, CPlayer::ZCATCH_CAUGHT_REASON_JOINING);
		else
			p->m_SpecExplicit = false;
	}
	
	/* end zCatch */
	
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), m_pController->GetTeamName(p->GetTeam()));
	SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), p->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	m_VoteUpdate = true;
	
	/* zCatch begin */
	SendChatTarget(ClientID, "Welcome to zCatch! Type /info for more.");
	if(g_Config.m_SvAllowJoin == 2 && leader)
	{
		char buf[128];
		str_format(buf, sizeof(buf), "You will join the game when '%s' dies", Server()->ClientName(leader->GetCID()));
		SendChatTarget(ClientID, buf);
	}
	/* zCatch end */
}

void CGameContext::OnClientConnected(int ClientID)
{
	// Check which team the player should be on
	const int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(ClientID);

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
	//players[client_id].init(client_id);
	//players[client_id].client_id = client_id;

	(void)m_pController->CheckTeamBalance();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	if(m_apPlayers[ClientID]->m_CaughtBy > CPlayer::ZCATCH_NOT_CAUGHT)
		m_apPlayers[m_apPlayers[ClientID]->m_CaughtBy]->ReleaseZCatchVictim(ClientID);
	
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(pReason);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
	}
}

// returns whether the player is allowed to chat, informs the player and mutes him if needed
bool CGameContext::MuteValidation(CPlayer *player)
{
	int i, ClientID = player->GetCID();
	if((i = Muted(ClientID)) > -1)
	{
		char aBuf[48];
		int Expires = (m_aMutes[i].m_Expires - Server()->Tick())/Server()->TickSpeed();
		str_format(aBuf, sizeof(aBuf), "You are muted for %d:%02d min.", Expires/60, Expires%60);
		SendChatTarget(ClientID, aBuf);
		return false;
	}
	//mute the player if he's spamming
	else if(g_Config.m_SvMuteDuration && ((player->m_ChatTicks += g_Config.m_SvChatValue) > g_Config.m_SvChatThreshold))
	{
		AddMute(ClientID, g_Config.m_SvMuteDuration, true);
		player->m_ChatTicks = 0;
		return false;
	}
	return true;
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(MsgID == NETMSGTYPE_CL_SAY)
	{
		CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
		int Team = pMsg->m_Team;
		if(Team)
			Team = pPlayer->GetTeam();
		else
			Team = CGameContext::CHAT_ALL;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
			return;

		pPlayer->m_LastChat = Server()->Tick();

		// check for invalid chars
		unsigned char *pMessage = (unsigned char *)pMsg->m_pMessage;
		while (*pMessage)
		{
			if(*pMessage < 32)
				*pMessage = ' ';
			pMessage++;
		}

		/* begin zCatch*/
		if(!str_comp_num("/", pMsg->m_pMessage, 1))
		{
			if (str_comp_nocase_num(pMsg->m_pMessage+1, "w ", 2) == 0)
			{
				char pWhisperMsg[256];
				str_copy(pWhisperMsg, pMsg->m_pMessage + 3, 256);
				Whisper(pPlayer->GetCID(), pWhisperMsg);
			}
			else if (str_comp_nocase_num(pMsg->m_pMessage+1, "whisper ", 8) == 0)
			{
				char pWhisperMsg[256];
				str_copy(pWhisperMsg, pMsg->m_pMessage + 9, 256);
				Whisper(pPlayer->GetCID(), pWhisperMsg);
			}
			else if (str_comp_nocase_num(pMsg->m_pMessage+1, "c ", 2) == 0)
			{
				char pWhisperMsg[256];
				str_copy(pWhisperMsg, pMsg->m_pMessage + 3, 256);
				Converse(pPlayer->GetCID(), pWhisperMsg);
			}
			else if (str_comp_nocase_num(pMsg->m_pMessage+1, "converse ", 9) == 0)
			{
				char pWhisperMsg[256];
				str_copy(pWhisperMsg, pMsg->m_pMessage + 10, 256);
				Converse(pPlayer->GetCID(), pWhisperMsg);
			}
			else if(!str_comp_nocase("w", pMsg->m_pMessage + 1) || !str_comp_nocase("whisper", pMsg->m_pMessage + 1)
					|| !str_comp_nocase("c", pMsg->m_pMessage + 1) || !str_comp_nocase("converse", pMsg->m_pMessage + 1)){
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Type /help to see how use this command.");
				SendChatTarget(ClientID, aBuf);
			}
			else if(!str_comp_nocase("info", pMsg->m_pMessage + 1))
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "zCatch %s by erd and Teetime, modified by Teelevision modified by Savander. See /help.", ZCATCH_VERSION);
				SendChatTarget(ClientID, aBuf);
				SendChatTarget(ClientID, "You are caught when killed and released when your killer dies. Catch everyone to win the round.");
				if(g_Config.m_SvLastStandingPlayers > 2)
				{
					str_format(aBuf, sizeof(aBuf), "If there are less than %d players, the round does not end and all players are released instead.", g_Config.m_SvLastStandingPlayers);
					SendChatTarget(ClientID, aBuf);
				}
#if defined(CONF_SQL)
				SendChatTarget(ClientID, "Commands list: top5, rank, help, victims, kills, w, whisper, c, converse");
#else
				SendChatTarget(ClientID, "Commands list: help, victims, kills, w, whisper, c, converse");
#endif
			}

#if defined(CONF_SQL)
			else if(!str_comp_nocase("top5", pMsg->m_pMessage + 1) || !str_comp_nocase_num("top5 ", pMsg->m_pMessage + 1, 5))
			{
				if(!str_comp_nocase_num("top5 ", pMsg->m_pMessage + 1, 5)){
					const char *Number = pMsg->m_pMessage + 5;
					  for(int i = 0; Number[i] != '\0'; i++){
					        if(isalpha(Number[i])){
					        	SendChatTarget(ClientID, "It must be a number.");
					        	return;
					        }
					  }
					  int i = str_toint(Number);
					  m_Ranking->ShowTop5(ClientID, i);
				}else{
					 m_Ranking->ShowTop5(ClientID, 0);
				}
			}
			else if(!str_comp_nocase("rank", pMsg->m_pMessage + 1) || !str_comp_nocase_num("rank ", pMsg->m_pMessage + 1, 5))
			{
				if(!str_comp_nocase_num("rank ", pMsg->m_pMessage + 1, 5)){
					m_Ranking->ShowRanking(ClientID, pMsg->m_pMessage + 6);
				}else{
					m_Ranking->ShowRanking(ClientID, Server()->ClientName(ClientID));
				}
			}
#endif
			else if(!str_comp_nocase("help", pMsg->m_pMessage + 1))
			{
#if defined(CONF_SQL)
				SendChatTarget(ClientID, "/rank <name> or /rank - show position in ranking.");
				SendChatTarget(ClientID, "/top5 or /top5 <number> - Top5 winners on server.");
#endif
				SendChatTarget(ClientID, "/victims - who is waiting for your death");
				SendChatTarget(ClientID, "/kills -  list of players you killed");
				SendChatTarget(ClientID, "/whisper <name> <msg>");
				SendChatTarget(ClientID, "/converse <id> <msg>");
			}
			else if(!str_comp_nocase("victims", pMsg->m_pMessage + 1))
			{
				if(pPlayer->m_zCatchNumVictims)
				{
					char aBuf[256], bBuf[256];
					CPlayer::CZCatchVictim *v = pPlayer->m_ZCatchVictims;
					str_format(aBuf, sizeof(aBuf), "%d player(s) await your death: ", pPlayer->m_zCatchNumVictims);
					while(v != NULL)
					{
						str_format(bBuf, sizeof(bBuf), (v == pPlayer->m_ZCatchVictims) ? "%s '%s'%s" : "%s, '%s'%s", aBuf, Server()->ClientName(v->ClientID), (v->Reason == CPlayer::ZCATCH_CAUGHT_REASON_JOINING) ? " (joined the game)" : "");
						str_copy(aBuf, bBuf, sizeof(aBuf));
						v = v->prev;
					}
					SendChatTarget(ClientID, aBuf);
				}
				else
				{
					SendChatTarget(ClientID, "No one awaits your death.");
				}
			}
			else if(!str_comp_nocase("kills", pMsg->m_pMessage + 1))
			{
				if(pPlayer->m_zCatchNumKillsInARow)
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "You caught %d player(s) since your last death.", pPlayer->m_zCatchNumKillsInARow);
					SendChatTarget(ClientID, aBuf);
				}
				else
				{
					SendChatTarget(ClientID, "You caught no one since your last death.");
				}
			}
			else
			{
				SendChatTarget(ClientID, "Unknown command, try /info");
			}
		}
		else
		{
			// send to chat
			if(MuteValidation(pPlayer))
				SendChat(ClientID, Team, pMsg->m_pMessage);
		}
		/* end zCatch */
	}
	else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
	{
		if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		int64 Now = Server()->Tick();
		pPlayer->m_LastVoteTry = Now;
		// zCatch - Only People who are explicit in Spectators can't vote!
		if(pPlayer->m_SpecExplicit) //zCatch
		{
			SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			return;
		}

		if(m_VoteCloseTime)
		{
			SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
			return;
		}

		char aChatmsg[512];
		
		// check voteban
		int left = Server()->ClientVotebannedTime(ClientID);
		if(left)
		{
			str_format(aChatmsg, sizeof(aChatmsg), "You are not allowed to vote for the next %d:%02d min.", left/60, left%60);
			SendChatTarget(ClientID, aChatmsg);
			return;
		}

		int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
		if(pPlayer->m_LastVoteCall && Timeleft > 0)
		{
			str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote", (Timeleft/Server()->TickSpeed())+1);
			SendChatTarget(ClientID, aChatmsg);
			return;
		}

		char aDesc[VOTE_DESC_LENGTH] = {0};
		char aCmd[VOTE_CMD_LENGTH] = {0};
		CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
		const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

		if(str_comp_nocase(pMsg->m_Type, "option") == 0)
		{
			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption)
			{
				if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
								pOption->m_aDescription, pReason);
					str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
					str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
					break;
				}

				pOption = pOption->m_pNext;
			}

			if(!pOption)
			{
				str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
				SendChatTarget(ClientID, aChatmsg);
				return;
			}
		}
		else if(g_Config.m_SvVoteForceReason && !pMsg->m_Reason[0])
		{
			SendChatTarget(ClientID, "You must give a reason for your vote");
			return;
		}
		else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
		{
			if(!g_Config.m_SvVoteKick)
			{
				SendChatTarget(ClientID, "Server does not allow voting to kick players");
				return;
			}

			if(g_Config.m_SvVoteKickMin)
			{
				int PlayerNum = 0;
				for(int i = 0; i < MAX_CLIENTS; ++i)
					if(m_apPlayers[i] && !m_apPlayers[i]->m_SpecExplicit) // zCatch - Count all Players who are not explicit in spectator
						++PlayerNum;

				if(PlayerNum < g_Config.m_SvVoteKickMin)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players on the server", g_Config.m_SvVoteKickMin);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
			}

			int KickID = str_toint(pMsg->m_Value);
			if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
			{
				SendChatTarget(ClientID, "Invalid client id to kick");
				return;
			}
			if(KickID == ClientID)
			{
				SendChatTarget(ClientID, "You can't kick yourself");
				return;
			}
			if(Server()->IsAuthed(KickID))
			{
				SendChatTarget(ClientID, "You can't kick admins");
				char aBufKick[128];
				str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
				SendChatTarget(KickID, aBufKick);
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
			str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
			if (!g_Config.m_SvVoteKickBantime)
				str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
			else
			{
				char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
				Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
				str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
			}
		}
		else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
		{
			if(!g_Config.m_SvVoteSpectate)
			{
				SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
				return;
			}

			int SpectateID = str_toint(pMsg->m_Value);
			if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientID, "Invalid client id to move");
				return;
			}
			if(SpectateID == ClientID)
			{
				SendChatTarget(ClientID, "You can't move yourself");
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		}

		if(aCmd[0])
		{
			SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
			StartVote(aDesc, aCmd, pReason);
			pPlayer->m_Vote = 1;
			pPlayer->m_VotePos = m_VotePos = 1;
			m_VoteCreator = ClientID;
			pPlayer->m_LastVoteCall = Now;
		}
	}
	else if (MsgID == NETMSGTYPE_CL_ISDDNET)
			{
				int Version = pUnpacker->GetInt();

				if (pUnpacker->Error())
				{
					if (pPlayer->m_ClientVersion < VERSION_DDRACE)
						pPlayer->m_ClientVersion = VERSION_DDRACE;
				}
				else if(pPlayer->m_ClientVersion < Version)
					pPlayer->m_ClientVersion = Version;
	}
	else if(MsgID == NETMSGTYPE_CL_VOTE)
	{
		if(!m_VoteCloseTime)
			return;

		if(pPlayer->m_Vote == 0)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
			if(!pMsg->m_Vote)
				return;

			pPlayer->m_Vote = pMsg->m_Vote;
			pPlayer->m_VotePos = ++m_VotePos;
			m_VoteUpdate = true;
		}
	}
	else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
	{
		CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

		if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			SendBroadcast("Teams are locked", ClientID);
			return;
		}

		if(pPlayer->m_TeamChangeTick > Server()->Tick())
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
			SendBroadcast(aBuf, ClientID);
			return;
		}

		// Switch team on given client and kill/respawn him
		if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
		{
			if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team) || !Server()->ClientIngame(pPlayer->m_CaughtBy))
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				pPlayer->SetTeam(pMsg->m_Team);
			}
			else
			{
				char aBuf[128];
				pPlayer->m_zCatchJoinSpecWhenReleased = !pPlayer->m_zCatchJoinSpecWhenReleased;
				str_format(aBuf, sizeof(aBuf), "You will join the %s when '%s' dies.", m_pController->GetTeamName(pPlayer->m_zCatchJoinSpecWhenReleased ? TEAM_SPECTATORS : TEAM_RED), Server()->ClientName(pPlayer->m_CaughtBy));
				SendChatTarget(ClientID, aBuf);
				return;
			}
		}
		else
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
			SendBroadcast(aBuf, ClientID);
		}
	}
	else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
	{
		CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

		if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID ||
			(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		pPlayer->m_LastSetSpectatorMode = Server()->Tick();
		if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
			SendChatTarget(ClientID, "Invalid spectator id used");
		else
			pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
	}
	else if (MsgID == NETMSGTYPE_CL_STARTINFO)
	{
		if(pPlayer->m_IsReady)
			return;

		CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set start infos
		Server()->SetClientName(ClientID, pMsg->m_pName);
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);
		str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
		pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
		pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
		pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
		m_pController->OnPlayerInfoChange(pPlayer);

		// send vote options
		CNetMsg_Sv_VoteClearOptions ClearMsg;
		Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

		CNetMsg_Sv_VoteOptionListAdd OptionMsg;
		int NumOptions = 0;
		OptionMsg.m_pDescription0 = "";
		OptionMsg.m_pDescription1 = "";
		OptionMsg.m_pDescription2 = "";
		OptionMsg.m_pDescription3 = "";
		OptionMsg.m_pDescription4 = "";
		OptionMsg.m_pDescription5 = "";
		OptionMsg.m_pDescription6 = "";
		OptionMsg.m_pDescription7 = "";
		OptionMsg.m_pDescription8 = "";
		OptionMsg.m_pDescription9 = "";
		OptionMsg.m_pDescription10 = "";
		OptionMsg.m_pDescription11 = "";
		OptionMsg.m_pDescription12 = "";
		OptionMsg.m_pDescription13 = "";
		OptionMsg.m_pDescription14 = "";
		CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
		while(pCurrent)
		{
			switch(NumOptions++)
			{
			case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
			case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
			case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
			case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
			case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
			case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
			case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
			case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
			case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
			case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
			case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
			case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
			case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
			case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
			case 14:
				{
					OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
					OptionMsg.m_NumOptions = NumOptions;
					Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
					OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
					NumOptions = 0;
					OptionMsg.m_pDescription1 = "";
					OptionMsg.m_pDescription2 = "";
					OptionMsg.m_pDescription3 = "";
					OptionMsg.m_pDescription4 = "";
					OptionMsg.m_pDescription5 = "";
					OptionMsg.m_pDescription6 = "";
					OptionMsg.m_pDescription7 = "";
					OptionMsg.m_pDescription8 = "";
					OptionMsg.m_pDescription9 = "";
					OptionMsg.m_pDescription10 = "";
					OptionMsg.m_pDescription11 = "";
					OptionMsg.m_pDescription12 = "";
					OptionMsg.m_pDescription13 = "";
					OptionMsg.m_pDescription14 = "";
				}
			}
			pCurrent = pCurrent->m_pNext;
		}
		if(NumOptions > 0)
		{
			OptionMsg.m_NumOptions = NumOptions;
			Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
		}

		// send tuning parameters to client
		SendTuningParams(ClientID);

		// client is ready to enter
		pPlayer->m_IsReady = true;
		CNetMsg_Sv_ReadyToEnter m;
		Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
	}
	else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
	{
		if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
			return;

		CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
		pPlayer->m_LastChangeInfo = Server()->Tick();

		// set infos
		char aOldName[MAX_NAME_LENGTH];
		str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
		Server()->SetClientName(ClientID, pMsg->m_pName);
		if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0 && Muted(ClientID) == -1)
		{
			char aChatText[256];
			str_format(aChatText, sizeof(aChatText), "'%s' changed name to '%s'", aOldName, Server()->ClientName(ClientID));
			SendChat(-1, CGameContext::CHAT_ALL, aChatText);
		}
		Server()->SetClientClan(ClientID, pMsg->m_pClan);
		Server()->SetClientCountry(ClientID, pMsg->m_Country);
		str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
		pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
		pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
		pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
		m_pController->OnPlayerInfoChange(pPlayer);
	}
	else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
	{
		CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

		if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
			return;

		pPlayer->m_LastEmote = Server()->Tick();

		SendEmoticon(ClientID, pMsg->m_Emoticon);
		CCharacter* pChr = pPlayer->GetCharacter();
					if(pChr)
					{
						switch(pMsg->m_Emoticon)
						{
						case EMOTICON_EXCLAMATION:
						case EMOTICON_GHOST:
						case EMOTICON_QUESTION:
						case EMOTICON_WTF:
								pChr->SetEmoteType(EMOTE_SURPRISE);
								break;
						case EMOTICON_DOTDOT:
						case EMOTICON_DROP:
						case EMOTICON_ZZZ:
								pChr->SetEmoteType(EMOTE_BLINK);
								break;
						case EMOTICON_EYES:
						case EMOTICON_HEARTS:
						case EMOTICON_MUSIC:
								pChr->SetEmoteType(EMOTE_HAPPY);
								break;
						case EMOTICON_OOP:
						case EMOTICON_SORRY:
						case EMOTICON_SUSHI:
								pChr->SetEmoteType(EMOTE_PAIN);
								break;
						case EMOTICON_DEVILTEE:
						case EMOTICON_SPLATTEE:
						case EMOTICON_ZOMG:
								pChr->SetEmoteType(EMOTE_ANGRY);
								break;
							default:
								pChr->SetEmoteType(EMOTE_NORMAL);
								break;
						}
						pChr->SetEmoteStop(Server()->Tick() + 2 * Server()->TickSpeed());
					}
	}
	else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
	{
		/* begin zCatch*/
		if(pPlayer->GetTeam() == TEAM_SPECTATORS || (pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick()) ||
				(pPlayer->m_LastKillTry+Server()->TickSpeed()*3 > Server()->Tick()))
			return;

		if(pPlayer->HasZCatchVictims())
		{
			int lastVictim = pPlayer->LastZCatchVictim();
			pPlayer->ReleaseZCatchVictim(CPlayer::ZCATCH_RELEASE_ALL, 1);
			int nextToRelease = pPlayer->LastZCatchVictim();
			char aBuf[128], bBuf[128];
			str_format(bBuf, sizeof(bBuf), ", next: %s", Server()->ClientName(nextToRelease));
			str_format(aBuf, sizeof(aBuf), "You released '%s'. (%d left%s)", Server()->ClientName(lastVictim), pPlayer->m_zCatchNumVictims, pPlayer->m_zCatchNumVictims > 0 ? bBuf : "");
			SendChatTarget(ClientID, aBuf);
			str_format(aBuf, sizeof(aBuf), "You were released by '%s'.", Server()->ClientName(ClientID));
			SendChatTarget(lastVictim, aBuf);
			return;
		}
		else if(g_Config.m_SvSuicideTime == 0)
		{
			SendChatTarget(ClientID, "Suicide is not allowed.");
		}
		else if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*g_Config.m_SvSuicideTime > Server()->Tick())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Only one suicide every %d seconds is allowed.", g_Config.m_SvSuicideTime);
			SendChatTarget(ClientID, aBuf);
		}
		else if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->m_FreezeTicks)
		{
			SendChatTarget(ClientID, "You can't kill yourself while you're frozen.");
		}
		else
		{
			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
			return;
		}
		pPlayer->m_LastKillTry = Server()->Tick();
		/* end zCatch*/
	}
}

void CGameContext::AddMute(const char* pIP, int Secs)
{
	int Pos = Muted(pIP);
	if(Pos > -1)	
		m_aMutes[Pos].m_Expires = Server()->TickSpeed() * Secs + Server()->Tick();	// overwrite mute
	else
		for(int i = 0; i < MAX_MUTES; i++)	// find free slot
			if(!m_aMutes[i].m_aIP[0])
			{
				str_copy(m_aMutes[i].m_aIP, pIP, sizeof(m_aMutes[i].m_aIP));
				m_aMutes[i].m_Expires = Server()->TickSpeed() * Secs + Server()->Tick();
				break;
			}
}

void CGameContext::AddMute(int ClientID, int Secs, bool Auto)
{
	char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
	Server()->GetClientAddr(ClientID, aAddrStr, sizeof(aAddrStr));
	AddMute(aAddrStr, Secs);
	
	char aBuf[128];
	if(Secs > 0)
		str_format(aBuf, sizeof(aBuf), "%s has been %smuted for %d:%02d min.", Server()->ClientName(ClientID), Auto ? "auto-" : "", Secs/60, Secs%60);
	else
		str_format(aBuf, sizeof(aBuf), "%s has been unmuted.", Server()->ClientName(ClientID));
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
	SendChatTarget(-1, aBuf);
}

int CGameContext::Muted(const char *pIP)
{
	CleanMutes();
	int Pos = -1;
	if(!pIP[0])
		return -1;
	for(int i = 0; i < MAX_MUTES; i++)
		if(!str_comp_num(pIP, m_aMutes[i].m_aIP, sizeof(m_aMutes[i].m_aIP)))
		{
			Pos = i;
			break;
		}
	return Pos;
}

int CGameContext::Muted(int ClientID)
{
	char aIP[NETADDR_MAXSTRSIZE] = {0};
	Server()->GetClientAddr(ClientID, aIP, sizeof(aIP));
	return Muted(aIP);
}

void CGameContext::CleanMutes()
{
	for(int i = 0; i < MAX_MUTES; i++)
		if(m_aMutes[i].m_Expires < Server()->Tick())
			m_aMutes[i].m_aIP[0] = 0;
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_pController->IsGameOver())
		return;

	pSelf->m_World.m_Paused ^= 1;
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;

	int CounterRed = 0;
	int CounterBlue = 0;
	int PlayerTeam = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			++PlayerTeam;
	PlayerTeam = (PlayerTeam+1)/2;
	
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			if(CounterRed == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
			else if(CounterBlue == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
			else
			{	
				if(rand() % 2)
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
					++CounterBlue;
				}
				else
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
					++CounterRed;
				}
			}
		}
	}

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = pResult->GetInteger(0);
	if(CID < 0 || CID >= MAX_CLIENTS || !pSelf->m_apPlayers[CID])
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid ClientID");
	else
		pSelf->AddMute(CID, pResult->GetInteger(1));
}

void CGameContext::ConMutes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[128];
	int Sec, Count = 0;
	pSelf->CleanMutes();
	for(int i = 0; i < MAX_MUTES; i++)
	{
		if(pSelf->m_aMutes[i].m_aIP[0] == 0)
			continue;
		
		Sec = (pSelf->m_aMutes[i].m_Expires - pSelf->Server()->Tick())/pSelf->Server()->TickSpeed();
		str_format(aBuf, sizeof(aBuf), "#%d: %s for %d:%02d min", i, pSelf->m_aMutes[i].m_aIP, Sec/60, Sec%60);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		Count++;
	}
	str_format(aBuf, sizeof(aBuf), "%d mute(s)", Count);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);	
}

void CGameContext::ConUnmuteID(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = pResult->GetInteger(0);
	if(CID < 0 || CID >= MAX_CLIENTS || !pSelf->m_apPlayers[CID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid ClientID");
	}
	else
		pSelf->AddMute(CID, 0);
}

void CGameContext::ConUnmuteIP(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int MuteID = pResult->GetInteger(0);
	char aBuf[128];
	
	if(MuteID < 0 || MuteID >= MAX_MUTES || pSelf->Muted(pSelf->m_aMutes[MuteID].m_aIP) == -1)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "mute not found");
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "unmuted %s", pSelf->m_aMutes[MuteID].m_aIP);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		pSelf->AddMute(pSelf->m_aMutes[MuteID].m_aIP, 0);
	}
}

void CGameContext::ConKill(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int CID = pResult->GetInteger(0);
	if(CID < 0 || CID >= MAX_CLIENTS || !pSelf->m_apPlayers[CID])
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid ClientID");
		return;
	}
	if(pSelf->m_apPlayers[CID]->GetCharacter() == 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Player is already dead");
		return;
	}
	pSelf->m_apPlayers[CID]->KillCharacter();
	// message to console and chat
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "'%s' has been killed by admin.", pSelf->Server()->ClientName(CID));
	pSelf->SendChatTarget(-1, aBuf);
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "si", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

	Console()->Register("mute", "ii", CFGFLAG_SERVER, ConMute, this, "Mutes a player for x sec");
	Console()->Register("unmute", "i", CFGFLAG_SERVER, ConUnmuteID, this, "Unmutes a player by its id");
	Console()->Register("unmuteid", "i", CFGFLAG_SERVER, ConUnmuteID, this, "Unmutes a player by its client id");
	Console()->Register("unmuteip", "i", CFGFLAG_SERVER, ConUnmuteIP, this, "Removes a mute by its index");
	Console()->Register("mutes", "", CFGFLAG_SERVER, ConMutes, this, "Show all mutes");
	
	Console()->Register("kill", "i", CFGFLAG_SERVER, ConKill, this, "Kill a player by id");
		
	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	/*if(str_comp(g_Config.m_SvGametype, "mod") == 0)
		m_pController = new CGameControllerMOD(this);
	else if(str_comp(g_Config.m_SvGametype, "ctf") == 0)
		m_pController = new CGameControllerCTF(this);
	else if(str_comp(g_Config.m_SvGametype, "tdm") == 0)
		m_pController = new CGameControllerTDM(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "zcatch") == 0)
		m_pController = new CGameController_zCatch(this);
	else
		m_pController = new CGameControllerDM(this);*/
	m_pController = new CGameController_zCatch(this);

#if defined(CONF_SQL)
		m_Ranking = new CRanking(this);
#endif
	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);




	/*
	num_spawn_points[0] = 0;
	num_spawn_points[1] = 0;
	num_spawn_points[2] = 0;
	*/

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

	//game.world.insert_entity(game.Controller);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

bool CGameContext::IsClientAimBot(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsAimBot;
}


bool CheckClientID2(int ClientID)
{
	dbg_assert(ClientID >= 0 || ClientID < MAX_CLIENTS,
			"The Client ID is wrong");
	if (ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return true;
}

void CGameContext::Whisper(int ClientID, char *pStr)
{
	char *pName;
	char *pMessage;
	int Error = 0;

	pStr = str_skip_whitespaces(pStr);

	int Victim;

	// add token
	if(*pStr == '"')
	{
		pStr++;

		pName = pStr; // we might have to process escape data
		while(1)
		{
			if(pStr[0] == '"')
				break;
			else if(pStr[0] == '\\')
			{
				if(pStr[1] == '\\')
					pStr++; // skip due to escape
				else if(pStr[1] == '"')
					pStr++; // skip due to escape
			}
			else if(pStr[0] == 0)
				Error = 1;

			pStr++;
		}

		// write null termination
		*pStr = 0;
		pStr++;

		for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
			if (str_comp(pName, Server()->ClientName(Victim)) == 0)
				break;

	}
	else
	{
		pName = pStr;
		while(1)
		{
			if(pStr[0] == 0)
			{
				Error = 1;
				break;
			}
			if(pStr[0] == ' ')
			{
				pStr[0] = 0;
				for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
					if (str_comp(pName, Server()->ClientName(Victim)) == 0)
						break;

				pStr[0] = ' ';

				if (Victim < MAX_CLIENTS)
					break;
			}
			pStr++;
		}
	}

	if(pStr[0] != ' ')
	{
		Error = 1;
	}

	*pStr = 0;
	pStr++;

	pMessage = pStr;

	char aBuf[256];

	if (Error)
	{
		str_format(aBuf, sizeof(aBuf), "Invalid whisper");
		SendChatTarget(ClientID, aBuf);
		return;
	}

	if (Victim >= MAX_CLIENTS || !CheckClientID2(Victim))
	{
		str_format(aBuf, sizeof(aBuf), "No player with name \"%s\" found", pName);
		SendChatTarget(ClientID, aBuf);
		return;
	}

	WhisperID(ClientID, Victim, pMessage);
}

void CGameContext::WhisperID(int ClientID, int VictimID, char *pMessage)
{
	if (!CheckClientID2(ClientID))
		return;

	if (!CheckClientID2(VictimID))
		return;

	if (m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_LastWhisperTo = VictimID;

	char aBuf[256];
	if(m_apPlayers[ClientID]->m_LastWhisperTo != ClientID){
		if (m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_ClientVersion >= VERSION_DDNET_WHISPER)
		{
			CNetMsg_Sv_Chat Msg;
			Msg.m_Team = CHAT_WHISPER_SEND;
			Msg.m_ClientID = VictimID;
			Msg.m_pMessage = pMessage;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "[→ %s] %s", Server()->ClientName(VictimID), pMessage);
			SendChatTarget(ClientID, aBuf);
		}

		if (m_apPlayers[VictimID] && m_apPlayers[VictimID]->m_ClientVersion >= VERSION_DDNET_WHISPER)
		{
			CNetMsg_Sv_Chat Msg2;
			Msg2.m_Team = CHAT_WHISPER_RECV;
			Msg2.m_ClientID = ClientID;
			Msg2.m_pMessage = pMessage;
			Server()->SendPackMsg(&Msg2, MSGFLAG_VITAL, VictimID);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "[← %s] %s", Server()->ClientName(ClientID), pMessage);
			SendChatTarget(VictimID, aBuf);
		}
	}else{
		str_format(aBuf, sizeof(aBuf), "You can't talk to yourself.");
		SendChatTarget(ClientID, aBuf);
	}
}

void CGameContext::Converse(int ClientID, char *pStr)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer)
		return;

	if (pPlayer->m_LastWhisperTo < 0)
		SendChatTarget(ClientID, "You do not have an ongoing conversation. Whisper to someone to start one");
	else
	{
		WhisperID(ClientID, pPlayer->m_LastWhisperTo, pStr);
	}
}


const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }




