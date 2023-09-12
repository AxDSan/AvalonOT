--[[

Command: /inbox
Description: Manage the store inbox of a specific creature by adding or removing items.
Parameters: 
  1. [target]: The name or ID of the creature whose store inbox you want to interact with.
  2. [action]: Either "add" or "remove," depending on whether you want to add or remove items from the inbox.
  3. [itemID]: The ID of the item you want to add or remove from the inbox.

Usage:
  - To add an item to a creature's inbox: /inbox [target], add, [itemID]
  - To remove an item from a creature's inbox: /inbox [target], remove, [itemID]

Example:
  - To add item ID 123 to the inbox of a creature named "John": /inbox John, add, 123
  - To remove item ID 456 from the inbox of a creature with ID 789: /inbox 789, remove, 456

]]

local inboxCommand = TalkAction("/inbox")

function inboxCommand.onSay(player, words, param)
	param = param:split(",")
	player:getPosition():sendMagicEffect(CONST_ME_TUTORIALSQUARE)
	local target = Creature(param[1])
	if target then
		local inbox = target:getSlotItem(CONST_SLOT_STORE_INBOX)
		local inboxSize = inbox:getSize()
		if inbox and inboxSize > 0 then
			if param[2] == "remove" then
				for i = 0, inboxSize do
					local item = inbox:getItem(i)
					if item and item:getId() == tonumber(param[3]) then
						local itemToDelete = Item(item.uid)
						if itemToDelete then
							itemToDelete:remove()
							player:say(item:getId() .. " removed")
						end
					end
				end
			elseif param[2] == "add" then
				inbox:addItem(tonumber(param[3]), 1, INDEX_WHEREEVER, FLAG_NOLIMIT)
				player:say(tonumber(param[3]) .. " added")
			end
		end
	else
		player:sendCancelMessage("Creature not found.")
	end

	return false
end

inboxCommand:separator(" ")
inboxCommand:groupType("god")
inboxCommand:register()
