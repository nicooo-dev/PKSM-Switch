/*
 *   This file is part of PKSM-Core
 *   Copyright (C) 2016-2022 Bernardo Giordano, Admiral Fish, piepie62
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#include "sav/SavPLA.hpp"
#include "pkx/PA8.hpp"
#include "utils/endian.hpp"
#include "utils/i18n.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <ranges>

namespace pksm
{
    SavPLA::SavPLA(const std::shared_ptr<u8[]>& dt, size_t length) : Sav8(dt, length)
    {
        game      = Game::PLA;
        Box       = KBox;
        Party     = KParty;
        Status    = KStatus;
        PokeDex   = KZukan;
        Items     = KItems;
        BoxLayout = KBoxLayout;
    }

    u16 SavPLA::TID(void) const
    {
        return LittleEndian::convertTo<u16>(getBlock(Status)->decryptedData() + 0x10);
    }

    void SavPLA::TID(u16 v)
    {
        LittleEndian::convertFrom<u16>(getBlock(Status)->decryptedData() + 0x10, v);
    }

    u16 SavPLA::SID(void) const
    {
        return LittleEndian::convertTo<u16>(getBlock(Status)->decryptedData() + 0x12);
    }

    void SavPLA::SID(u16 v)
    {
        LittleEndian::convertFrom<u16>(getBlock(Status)->decryptedData() + 0x12, v);
    }

    GameVersion SavPLA::version(void) const
    {
        return GameVersion(getBlock(Status)->decryptedData()[0x14]);
    }

    void SavPLA::version(GameVersion v)
    {
        getBlock(Status)->decryptedData()[0x14] = u8(v);
    }

    Gender SavPLA::gender(void) const
    {
        return Gender{getBlock(Status)->decryptedData()[0x15]};
    }

    void SavPLA::gender(Gender v)
    {
        getBlock(Status)->decryptedData()[0x15] = u8(v);
    }

    Language SavPLA::language(void) const
    {
        return Language(getBlock(Status)->decryptedData()[0x17]);
    }

    void SavPLA::language(Language v)
    {
        getBlock(Status)->decryptedData()[0x17] = u8(v);
    }

    std::string SavPLA::otName(void) const
    {
        return StringUtils::getString(getBlock(Status)->decryptedData(), 0x20, 13);
    }

    void SavPLA::otName(const std::string_view& v)
    {
        StringUtils::setString(getBlock(Status)->decryptedData(), v, 0x20, 13);
    }

    u32 SavPLA::money(void) const
    {
        return LittleEndian::convertTo<u32>(getBlock(KMoney)->decryptedData());
    }

    void SavPLA::money(u32 v)
    {
        LittleEndian::convertFrom<u32>(getBlock(KMoney)->decryptedData(), v);
    }

    u16 SavPLA::playedHours(void) const
    {
        // 8b structure in PKHeX; use first 2 bytes as hours, next minutes/seconds as bytes.
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return 0;
        }
        return LittleEndian::convertTo<u16>(block->decryptedData());
    }

    void SavPLA::playedHours(u16 v)
    {
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return;
        }
        LittleEndian::convertFrom<u16>(block->decryptedData(), v);
    }

    u8 SavPLA::playedMinutes(void) const
    {
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return 0;
        }
        return block->decryptedData()[2];
    }

    void SavPLA::playedMinutes(u8 v)
    {
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return;
        }
        block->decryptedData()[2] = v;
    }

    u8 SavPLA::playedSeconds(void) const
    {
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return 0;
        }
        return block->decryptedData()[3];
    }

    void SavPLA::playedSeconds(u8 v)
    {
        auto block = getBlock(0xC4FA7C8C);
        if (!block)
        {
            return;
        }
        block->decryptedData()[3] = v;
    }

    void SavPLA::item(const Item& item, Pouch pouch, u16 slot)
    {
        (void)item;
        (void)pouch;
        (void)slot;
    }

    std::unique_ptr<Item> SavPLA::item(Pouch, u16) const
    {
        return nullptr;
    }

    SmallVector<std::pair<Sav::Pouch, int>, 15> SavPLA::pouches(void) const
    {
        return {};
    }

    SmallVector<std::pair<Sav::Pouch, std::span<const int>>, 15> SavPLA::validItems(void) const
    {
        return {};
    }

    u8 SavPLA::currentBox() const
    {
        auto block = getBlock(KCurrentBox);
        if (!block)
        {
            return 0;
        }
        return block->decryptedData()[0];
    }

    void SavPLA::currentBox(u8 box)
    {
        auto block = getBlock(KCurrentBox);
        if (!block)
        {
            return;
        }
        block->decryptedData()[0] = box;
    }

    u8 SavPLA::unlockedBoxes() const
    {
        auto block = getBlock(KBoxesUnlocked);
        if (!block)
        {
            return maxBoxes();
        }
        return block->decryptedData()[0];
    }

    void SavPLA::unlockedBoxes(u8 v)
    {
        auto block = getBlock(KBoxesUnlocked);
        if (!block)
        {
            return;
        }
        block->decryptedData()[0] = v;
    }

    std::string SavPLA::boxName(u8 box) const
    {
        return StringUtils::getString(getBlock(BoxLayout)->decryptedData(), box * 0x22, 17);
    }

    void SavPLA::boxName(u8 box, const std::string_view& name)
    {
        StringUtils::setString(getBlock(BoxLayout)->decryptedData(), name, box * 0x22, 17);
    }

    u8 SavPLA::boxWallpaper(u8 box) const
    {
        auto block = getBlock(KBoxWallpapers);
        if (!block)
        {
            return 0;
        }
        return block->decryptedData()[box];
    }

    void SavPLA::boxWallpaper(u8 box, u8 v)
    {
        auto block = getBlock(KBoxWallpapers);
        if (!block)
        {
            return;
        }
        block->decryptedData()[box] = v;
    }

    u32 SavPLA::boxOffset(u8 box, u8 slot) const
    {
        return PA8::BOX_LENGTH * slot + PA8::BOX_LENGTH * 30 * box;
    }

    u32 SavPLA::partyOffset(u8 slot) const
    {
        return PA8::PARTY_LENGTH * slot;
    }

    u8 SavPLA::partyCount(void) const
    {
        return getBlock(Party)->decryptedData()[PA8::PARTY_LENGTH * 6];
    }

    void SavPLA::partyCount(u8 count)
    {
        getBlock(Party)->decryptedData()[PA8::PARTY_LENGTH * 6] = count;
    }

    std::unique_ptr<PKX> SavPLA::pkm(u8 slot) const
    {
        u32 offset = partyOffset(slot);
        return PKX::getPKM<PA8>(getBlock(Party)->decryptedData() + offset, PA8::PARTY_LENGTH);
    }

    std::unique_ptr<PKX> SavPLA::pkm(u8 box, u8 slot) const
    {
        u32 offset = boxOffset(box, slot);
        return PKX::getPKM<PA8>(getBlock(Box)->decryptedData() + offset, PA8::BOX_LENGTH);
    }

    void SavPLA::pkm(const PKX& pk, u8 box, u8 slot, bool applyTrade)
    {
        if (pk.getLength() == PA8::PARTY_LENGTH || pk.getLength() == PA8::BOX_LENGTH)
        {
            auto pa8 = pk.partyClone();
            if (applyTrade)
            {
                trade(*pa8);
            }
            // Box storage uses stored size (0x168)
            std::ranges::copy(pa8->rawData().subspan(0, PA8::BOX_LENGTH),
                getBlock(Box)->decryptedData() + boxOffset(box, slot));
        }
    }

    void SavPLA::pkm(const PKX& pk, u8 slot)
    {
        if (pk.getLength() == PA8::PARTY_LENGTH || pk.getLength() == PA8::BOX_LENGTH)
        {
            auto pa8 = pk.partyClone();
            pa8->encrypt();
            std::ranges::copy(pa8->rawData(), getBlock(Party)->decryptedData() + partyOffset(slot));
        }
    }

    void SavPLA::cryptBoxData(bool crypted)
    {
        for (u8 box = 0; box < maxBoxes(); box++)
        {
            for (u8 slot = 0; slot < 30; slot++)
            {
                std::unique_ptr<PKX> pa8 = PKX::getPKM<PA8>(
                    getBlock(Box)->decryptedData() + boxOffset(box, slot), PA8::BOX_LENGTH, true);
                if (!crypted)
                {
                    pa8->encrypt();
                }
            }
        }
    }
}