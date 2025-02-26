---@class BossLever
---@field private name string
---@field private bossPosition Position
---@field private createBoss function
---@field private timeToFightAgain number
---@field private timeToDefeat number
---@field private requiredLevel number
---@field private storage number
---@field private disabled boolean
---@field private onUseExtra function
---@field private _position Position
---@field private _uid number
---@field private _aid number
---@field private playerPositions {pos: Position, teleport: Position}[]
---@field private area {from: Position, to: Position}
---@field private monsters {name: string, pos: Position}[]
---@field private exit Position
---@field private timeoutEvent Event
BossLever = {}

--[[
local config = {
	boss = {
		name = "Faceless Bane",
		position = Position(33617, 32561, 13)
	}
	requiredLevel = 250,
	timeToFightAgain = 10 * 60 * 60, -- In seconds
	playerPositions = {
		{ pos = Position(33638, 32562, 13), teleport = Position(33617, 32567, 13) },
		{ pos = Position(33639, 32562, 13), teleport = Position(33617, 32567, 13) },
		{ pos = Position(33640, 32562, 13), teleport = Position(33617, 32567, 13) },
		{ pos = Position(33641, 32562, 13), teleport = Position(33617, 32567, 13) },
		{ pos = Position(33642, 32562, 13), teleport = Position(33617, 32567, 13) },
	},
	specPos = {
		from = Position(33607, 32553, 13),
		to = Position(33627, 32570, 13)
	},
	onUseExtra = function(player)
		player:teleportTo(Position(33618, 32523, 15))
		player:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	end,
	exit = Position(33618, 32523, 15),
	storage = Storage.Quest.U12_00.TheDreamCourts.FacelessBaneTime
}
]]
setmetatable(BossLever, {
	---@param self BossLever
	---@param config table
	__call = function(self, config)
		local boss = config.boss
		if not boss then
			error("BossLever: boss is required")
		end
		return setmetatable({
			name = boss.name,
			encounter = config.encounter,
			bossPosition = boss.position,
			timeToFightAgain = config.timeToFightAgain or configManager.getNumber(configKeys.BOSS_DEFAULT_TIME_TO_FIGHT_AGAIN),
			timeToDefeat = config.timeToDefeat or configManager.getNumber(configKeys.BOSS_DEFAULT_TIME_TO_DEFEAT),
			requiredLevel = config.requiredLevel or 0,
			createBoss = boss.createFunction,
			storage = config.storage,
			disabled = config.disabled,
			playerPositions = config.playerPositions,
			onUseExtra = config.onUseExtra or function() end,
			exit = config.exit,
			area = config.specPos,
			monsters = config.monsters or {},
			_position = nil,
			_uid = nil,
			_aid = nil,
		}, { __index = BossLever })
	end,
})

---@param self BossLever
---@param position Position
---@return BossLever
function BossLever:position(position)
	self._position = position
	return self
end

---@param self BossLever
---@param uid number
---@return BossLever
function BossLever:uid(uid)
	self._uid = uid
	return self
end

---@param self BossLever
---@param aid number
---@return BossLever
function BossLever:aid(aid)
	self._aid = aid
	return self
end

---@param player Player
---@return boolean
function BossLever:onUse(player)
	local isParticipant = false
	for _, v in ipairs(self.playerPositions) do
		if v.pos == player:getPosition() then
			isParticipant = true
		end
	end
	if not isParticipant then
		return false
	end

	if self.disabled then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "The boss is temporarily disabled.")
		return true
	end

	local zone = self:getZone()
	if zone:countPlayers(IgnoredByMonsters) > 0 then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "There's already someone fighting with " .. self.name .. ".")
		return true
	end

	local lever = Lever()
	lever:setPositions(self.playerPositions)
	lever:setCondition(function(creature)
		if not creature or not creature:isPlayer() then
			return true
		end

		if creature:getLevel() < self.requiredLevel then
			creature:sendTextMessage(MESSAGE_EVENT_ADVANCE, "All the players need to be level " .. self.requiredLevel .. " or higher.")
			return false
		end

		if creature:getStorageValue(self.storage) > os.time() then
			local info = lever:getInfoPositions()
			for _, v in pairs(info) do
				local newPlayer = v.creature
				if newPlayer then
					newPlayer:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You or a member in your team have to wait " .. self.timeToFightAgain / 60 / 60 .. " hours to face " .. self.name .. " again!")
					if newPlayer:getStorageValue(self.storage) > os.time() then
						newPlayer:getPosition():sendMagicEffect(CONST_ME_POFF)
					end
				end
			end
			return false
		end
		self.onUseExtra(creature)
		if self.encounter then
			local encounter = Encounter(self.encounter)
			encounter:start()
		end
		return true
	end)

	lever:checkPositions()
	if lever:checkConditions() then
		zone:removeMonsters()
		for _, monster in pairs(self.monsters) do
			Game.createMonster(monster.name, monster.pos, true, true)
		end
		if self.createBoss then
			if not self.createBoss() then
				return true
			end
		elseif self.bossPosition then
			local monster = Game.createMonster(self.name, self.bossPosition, true, true)
			if not monster then
				return true
			end
		end
		lever:teleportPlayers()
		lever:setStorageAllPlayers(self.storage, os.time() + self.timeToFightAgain)
		if self.timeoutEvent then
			stopEvent(self.timeoutEvent)
			self.timeoutEvent = nil
		end
		self.timeoutEvent = addEvent(zone.removePlayers, self.timeToDefeat * 1000, zone)
	end
	return true
end

local function toKey(str)
	return str:lower():gsub(" ", "-"):gsub("%s+", "")
end

---@param Zone
function BossLever:getZone()
	return Zone("boss." .. toKey(self.name))
end

---@param self BossLever
---@return boolean
function BossLever:register()
	local missingParams = {}
	if not self.name then
		table.insert(missingParams, "boss.name")
	end
	if not self.storage then
		table.insert(missingParams, "storage")
	end
	if not self.playerPositions then
		table.insert(missingParams, "playerPositions")
	end
	if not self.area then
		table.insert(missingParams, "specPos")
	end
	if not self.exit then
		table.insert(missingParams, "exit")
	end
	if not self._position and not self._uid and not self._aid then
		table.insert(missingParams, "position or uid or aid")
	end
	if #missingParams > 0 then
		local name = self.name or "unknown"
		logger.error("BossLever:register() - boss with name {} missing parameters: {}", name, table.concat(missingParams, ", "))
		return false
	end

	local zone = self:getZone()

	zone:addArea(self.area.from, self.area.to)
	zone:blockFamiliars()
	zone:setRemoveDestination(self.exit)

	local action = Action()
	action.onUse = function(player)
		self:onUse(player)
	end
	if self._position then
		action:position(self._position)
	end
	if self._uid then
		action:uid(self._uid)
	end
	if self._aid then
		action:aid(self._aid)
	end
	action:register()
	return true
end
