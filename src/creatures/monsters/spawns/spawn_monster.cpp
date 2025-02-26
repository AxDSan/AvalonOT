/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "pch.hpp"

#include "creatures/monsters/spawns/spawn_monster.hpp"
#include "game/game.hpp"
#include "creatures/monsters/monster.hpp"
#include "game/scheduling/scheduler.hpp"
#include "game/scheduling/events_scheduler.hpp"
#include "lua/creature/events.hpp"
#include "lua/callbacks/event_callback.hpp"
#include "lua/callbacks/events_callbacks.hpp"
#include "utils/pugicast.hpp"

static constexpr int32_t MONSTER_MINSPAWN_INTERVAL = 1000; // 1 second
static constexpr int32_t MONSTER_MAXSPAWN_INTERVAL = 86400000; // 1 day

bool SpawnsMonster::loadFromXML(const std::string &filemonstername) {
	if (isLoaded()) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filemonstername.c_str());
	if (!result) {
		printXMLError(__FUNCTION__, filemonstername, result);
		return false;
	}

	this->filemonstername = filemonstername;
	loaded = true;

	uint32_t eventschedule = g_eventsScheduler().getSpawnMonsterSchedule();
	std::string boostedNameGet = g_game().getBoostedMonsterName();

	for (auto spawnMonsterNode : doc.child("monsters").children()) {
		Position centerPos(
			pugi::cast<uint16_t>(spawnMonsterNode.attribute("centerx").value()),
			pugi::cast<uint16_t>(spawnMonsterNode.attribute("centery").value()),
			pugi::cast<uint16_t>(spawnMonsterNode.attribute("centerz").value())
		);

		int32_t radius;
		pugi::xml_attribute radiusAttribute = spawnMonsterNode.attribute("radius");
		if (radiusAttribute) {
			radius = pugi::cast<int32_t>(radiusAttribute.value());
		} else {
			radius = -1;
		}

		if (!spawnMonsterNode.first_child()) {
			g_logger().warn("[SpawnsMonster::loadFromXml] - Empty spawn at position: {} with radius: {}", centerPos.toString(), radius);
			continue;
		}

		spawnMonsterList.emplace_front(centerPos, radius);
		SpawnMonster &spawnMonster = spawnMonsterList.front();

		for (auto childMonsterNode : spawnMonsterNode.children()) {
			if (strcasecmp(childMonsterNode.name(), "monster") == 0) {
				pugi::xml_attribute nameAttribute = childMonsterNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Direction dir;

				pugi::xml_attribute directionAttribute = childMonsterNode.attribute("direction");
				if (directionAttribute) {
					dir = static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value()));
				} else {
					dir = DIRECTION_NORTH;
				}

				auto xOffset = pugi::cast<int16_t>(childMonsterNode.attribute("x").value());
				auto yOffset = pugi::cast<int16_t>(childMonsterNode.attribute("y").value());
				Position pos(
					static_cast<uint16_t>(centerPos.x + xOffset),
					static_cast<uint16_t>(centerPos.y + yOffset),
					centerPos.z
				);

				int32_t boostedrate = 1;

				if (nameAttribute.value() == boostedNameGet) {
					boostedrate = 2;
				}

				uint32_t interval = pugi::cast<uint32_t>(childMonsterNode.attribute("spawntime").value()) * 1000 * 100 / std::max((uint32_t)1, (g_configManager().getNumber(RATE_SPAWN) * boostedrate * eventschedule));
				if (interval >= MONSTER_MINSPAWN_INTERVAL && interval <= MONSTER_MAXSPAWN_INTERVAL) {
					spawnMonster.addMonster(nameAttribute.as_string(), pos, dir, static_cast<uint32_t>(interval));
				} else {
					if (interval <= MONSTER_MINSPAWN_INTERVAL) {
						g_logger().warn("[SpawnsMonster::loadFromXml] - {} {} spawntime cannot be less than {} seconds, set to {} by default.", nameAttribute.as_string(), pos.toString(), MONSTER_MINSPAWN_INTERVAL / 1000, MONSTER_MINSPAWN_INTERVAL / 1000);
						spawnMonster.addMonster(nameAttribute.as_string(), pos, dir, MONSTER_MINSPAWN_INTERVAL);
					} else {
						g_logger().warn("[SpawnsMonster::loadFromXml] - {} {} spawntime can not be more than {} seconds", nameAttribute.as_string(), pos.toString(), MONSTER_MAXSPAWN_INTERVAL / 1000);
					}
				}
			}
		}
	}
	return true;
}

void SpawnsMonster::startup() {
	if (!isLoaded() || isStarted()) {
		return;
	}

	for (SpawnMonster &spawnMonster : spawnMonsterList) {
		spawnMonster.startup();
	}

	started = true;
}

void SpawnsMonster::clear() {
	for (SpawnMonster &spawnMonster : spawnMonsterList) {
		spawnMonster.stopEvent();
	}
	spawnMonsterList.clear();

	loaded = false;
	started = false;
	filemonstername.clear();
}

bool SpawnsMonster::isInZone(const Position &centerPos, int32_t radius, const Position &pos) {
	if (radius == -1) {
		return true;
	}

	return ((pos.getX() >= centerPos.getX() - radius) && (pos.getX() <= centerPos.getX() + radius) && (pos.getY() >= centerPos.getY() - radius) && (pos.getY() <= centerPos.getY() + radius));
}

void SpawnMonster::startSpawnMonsterCheck() {
	if (checkSpawnMonsterEvent == 0) {
		checkSpawnMonsterEvent = g_scheduler().addEvent(getInterval(), std::bind(&SpawnMonster::checkSpawnMonster, this), "SpawnMonster::checkSpawnMonster");
	}
}

SpawnMonster::~SpawnMonster() {
	for (const auto &it : spawnedMonsterMap) {
		Monster* monster = it.second;
		monster->setSpawnMonster(nullptr);
		monster->decrementReferenceCounter();
	}
}

bool SpawnMonster::findPlayer(const Position &pos) {
	SpectatorHashSet spectators;
	g_game().map.getSpectators(spectators, pos, false, true);
	for (Creature* spectator : spectators) {
		if (!spectator->getPlayer()->hasFlag(PlayerFlags_t::IgnoredByMonsters)) {
			return true;
		}
	}
	return false;
}

bool SpawnMonster::isInSpawnMonsterZone(const Position &pos) {
	return SpawnsMonster::isInZone(centerPos, radius, pos);
}

bool SpawnMonster::spawnMonster(uint32_t spawnMonsterId, const std::shared_ptr<MonsterType> monsterType, const Position &pos, Direction dir, bool startup /*= false*/) {
	std::unique_ptr<Monster> monster_ptr(new Monster(monsterType));
	if (startup) {
		// No need to send out events to the surrounding since there is no one out there to listen!
		if (!g_game().internalPlaceCreature(monster_ptr.get(), pos, true)) {
			return false;
		}
	} else {
		if (!g_game().placeCreature(monster_ptr.get(), pos, false, true)) {
			return false;
		}
	}

	Monster* monster = monster_ptr.release();
	monster->setDirection(dir);
	monster->setSpawnMonster(this);
	monster->setMasterPos(pos);
	monster->incrementReferenceCounter();

	spawnedMonsterMap.insert(spawned_pair(spawnMonsterId, monster));
	spawnMonsterMap[spawnMonsterId].lastSpawn = OTSYS_TIME();
	g_events().eventMonsterOnSpawn(monster, pos);
	g_callbacks().executeCallback(EventCallback_t::monsterOnSpawn, &EventCallback::monsterOnSpawn, monster, pos);
	return true;
}

void SpawnMonster::startup() {
	for (const auto &it : spawnMonsterMap) {
		uint32_t spawnMonsterId = it.first;
		const spawnBlock_t &sb = it.second;
		spawnMonster(spawnMonsterId, sb.monsterType, sb.pos, sb.direction, true);
	}
}

void SpawnMonster::checkSpawnMonster() {
	checkSpawnMonsterEvent = 0;

	cleanup();

	uint32_t spawnMonsterCount = 0;

	for (auto &it : spawnMonsterMap) {
		uint32_t spawnMonsterId = it.first;
		if (spawnedMonsterMap.find(spawnMonsterId) != spawnedMonsterMap.end()) {
			continue;
		}

		spawnBlock_t &sb = it.second;
		if (!sb.monsterType->canSpawn(sb.pos)) {
			sb.lastSpawn = OTSYS_TIME();
			continue;
		}

		if (OTSYS_TIME() >= sb.lastSpawn + sb.interval) {
			if (sb.monsterType->info.isBlockable && findPlayer(sb.pos)) {
				sb.lastSpawn = OTSYS_TIME();
				continue;
			}

			if (sb.monsterType->info.isBlockable) {
				spawnMonster(spawnMonsterId, sb.monsterType, sb.pos, sb.direction);
			} else {
				scheduleSpawn(spawnMonsterId, sb, 3 * NONBLOCKABLE_SPAWN_MONSTER_INTERVAL);
			}

			if (++spawnMonsterCount >= static_cast<uint32_t>(g_configManager().getNumber(RATE_SPAWN))) {
				break;
			}
		}
	}

	if (spawnedMonsterMap.size() < spawnMonsterMap.size()) {
		checkSpawnMonsterEvent = g_scheduler().addEvent(getInterval(), std::bind(&SpawnMonster::checkSpawnMonster, this), __FUNCTION__);
	}
}

void SpawnMonster::scheduleSpawn(uint32_t spawnMonsterId, spawnBlock_t &sb, uint16_t interval) {
	if (interval <= 0) {
		spawnMonster(spawnMonsterId, sb.monsterType, sb.pos, sb.direction);
	} else {
		g_game().addMagicEffect(sb.pos, CONST_ME_TELEPORT);
		g_scheduler().addEvent(1400, std::bind(&SpawnMonster::scheduleSpawn, this, spawnMonsterId, sb, interval - NONBLOCKABLE_SPAWN_MONSTER_INTERVAL), __FUNCTION__);
	}
}

void SpawnMonster::cleanup() {
	auto it = spawnedMonsterMap.begin();
	while (it != spawnedMonsterMap.end()) {
		uint32_t spawnMonsterId = it->first;
		Monster* monster = it->second;
		if (monster->isRemoved()) {
			spawnMonsterMap[spawnMonsterId].lastSpawn = OTSYS_TIME();
			monster->decrementReferenceCounter();
			it = spawnedMonsterMap.erase(it);
		} else {
			++it;
		}
	}
}

bool SpawnMonster::addMonster(const std::string &name, const Position &pos, Direction dir, uint32_t scheduleInterval) {
	const auto monsterType = g_monsters().getMonsterType(name);
	if (!monsterType) {
		g_logger().error("Can not find {}", name);
		return false;
	}

	this->interval = std::min(this->interval, scheduleInterval);

	spawnBlock_t sb;
	sb.monsterType = monsterType;
	sb.pos = pos;
	sb.direction = dir;
	sb.interval = scheduleInterval;
	sb.lastSpawn = 0;

	uint32_t spawnMonsterId = spawnMonsterMap.size() + 1;
	spawnMonsterMap[spawnMonsterId] = sb;
	return true;
}

void SpawnMonster::removeMonster(Monster* monster) {
	for (auto it = spawnedMonsterMap.begin(), end = spawnedMonsterMap.end(); it != end; ++it) {
		if (it->second == monster) {
			monster->decrementReferenceCounter();
			spawnedMonsterMap.erase(it);
			break;
		}
	}
}

void SpawnMonster::stopEvent() {
	if (checkSpawnMonsterEvent != 0) {
		g_scheduler().stopEvent(checkSpawnMonsterEvent);
		checkSpawnMonsterEvent = 0;
	}
}
