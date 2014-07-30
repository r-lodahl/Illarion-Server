//  illarionserver - server for the game Illarion
//  Copyright 2011 Illarion e.V.
//
//  This file is part of illarionserver.
//
//  illarionserver is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Affero General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  illarionserver is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Affero General Public License for more details.
//
//  You should have received a copy of the GNU Affero General Public License
//  along with illarionserver.  If not, see <http://www.gnu.org/licenses/>.


#include "WorldMap.hpp"
#include "Map.hpp"
#include "Logger.hpp"

#include <algorithm>
#include <stdexcept>
#include <boost/algorithm/string/replace.hpp>
#include <chrono>

void WorldMap::clear() {
    maps.clear();
    world_map.clear();
}

bool WorldMap::mapInRangeOf(const position &upperleft, unsigned short dx,
                            unsigned short dy) const {
    const MAP_POSITION downright(upperleft.x + dx - 1, upperleft.y + dy - 1);

    return std::any_of(maps.begin(), maps.end(), [&](const map_t &map) {
        return map->intersects(upperleft, downright, upperleft.z);
    });
}

auto WorldMap::findAllMapsInRangeOf(char rnorth, char rsouth, char reast,
                                    char rwest,
                                    position pos) const -> map_vector_t {
    map_vector_t result;

    const MAP_POSITION upperleft(pos.x - rwest, pos.y - rnorth);
    const MAP_POSITION downright(pos.x + reast, pos.y + rsouth);

    for (const auto &map : maps) {
        if (map->intersects(upperleft, downright, pos.z)) {
            result.push_back(map);
        }
    }

    return result;
}

auto WorldMap::findMapForPos(const position &pos) const -> map_t {
    try {
        return world_map.at(pos);
    } catch (std::out_of_range &e) {
        return {};
    }
}

bool WorldMap::InsertMap(WorldMap::map_t newMap) {
    if (newMap) {
        for (auto it = maps.begin(); it < maps.end(); ++it) {
            if (*it == newMap) {
                return false;
            }
        }

        maps.push_back(newMap);

        auto z = newMap->Z_Level;

        for (auto x = newMap->Min_X; x <= newMap->Max_X; ++x) {
            for (auto y = newMap->Min_Y; y <= newMap->Max_Y; ++y) {
                position p(x, y, z);
                world_map[p] = newMap;
            }
        }

        return true;
    }

    return false;

}

bool WorldMap::allMapsAged() {
    using std::chrono::steady_clock;
    using std::chrono::milliseconds;

    auto startTime = steady_clock::now();

    while (ageIndex < maps.size() && steady_clock::now() - startTime < milliseconds(10)) {
        maps[ageIndex++]->age();
    }

    if (ageIndex < maps.size()) {
        return false;
    }

    ageIndex = 0;
    return true;
}

bool WorldMap::exportTo(const std::string &exportDir) const {
    for (auto mapIt = maps.begin(); mapIt != maps.end(); ++mapIt) {
        int16_t minX = (*mapIt)->GetMinX();
        int16_t minY = (*mapIt)->GetMinY();
        // create base filename
        std::string filebase = exportDir + "e_" + std::to_string(minX)
                               + "_" + std::to_string(minY)
                               + "_" + std::to_string((*mapIt)->Z_Level) + ".";
        // export fields file
        std::ofstream fieldsf((filebase + "tiles.txt").c_str());
        // export items file
        std::ofstream itemsf((filebase + "items.txt").c_str());
        // export warps file
        std::ofstream warpsf((filebase + "warps.txt").c_str());

        if (!fieldsf.good() || !itemsf.good() || !warpsf.good()) {
            Logger::error(LogFacility::World) << "Could not open output files for item export: " << filebase << "*.txt" << Log::end;
            return false;
        }

        // export tiles header
        fieldsf << "V: 2" << std::endl;
        fieldsf << "L: " << (*mapIt)->Z_Level << std::endl;
        fieldsf << "X: " << minX << std::endl;
        fieldsf << "Y: " << minY << std::endl;
        fieldsf << "W: " << (*mapIt)->GetWidth() << std::endl;
        fieldsf << "H: " << (*mapIt)->GetHeight() << std::endl;

        // iterate over the map and export...
        short int x, y;

        for (y = minY; y <= (*mapIt)->GetMaxY(); ++y) {
            for (x = minX; x <= (*mapIt)->GetMaxX(); ++x) {
                Field field;

                if ((*mapIt)->GetCFieldAt(field, x, y)) {
                    fieldsf << x-minX << ";" << y-minY << ";" << field.getTileCode() << ";" << field.getMusicId() << std::endl;

                    if (field.IsWarpField()) {
                        position target;
                        field.GetWarpField(target);
                        warpsf << x-minX << ";" << y-minY << ";" << target.x << ";" << target.y << ";" << target.z << std::endl;
                    }

                    ITEMVECTOR itemsv;
                    field.giveExportItems(itemsv);

                    for (auto it = itemsv.cbegin(); it != itemsv.cend(); ++it) {
                        itemsf << x-minX << ";" << y-minY << ";" << it->getId() << ";" << it->getQuality();

                        for (auto dataIt = it->getDataBegin(); dataIt != it->getDataEnd(); ++dataIt) {
                            using boost::algorithm::replace_all;

                            std::string key = dataIt->first;
                            std::string value = dataIt->second;

                            replace_all(key, "\\", "\\\\");
                            replace_all(key, "=", "\\=");
                            replace_all(key, ";", "\\;");
                            replace_all(value, "\\", "\\\\");
                            replace_all(value, "=", "\\=");
                            replace_all(value, ";", "\\;");

                            itemsf << ";" << key << "=" << value;
                        }

                        itemsf << std::endl;
                    }
                }
            }
        }

        fieldsf.close();
        itemsf.close();
        warpsf.close();

    }

    return true;
}

void WorldMap::saveToDisk(const std::string &prefix) const {
    std::ofstream mapinitfile((prefix + "_initmaps").c_str(), std::ios::binary | std::ios::out | std::ios::trunc);

    if (!mapinitfile.good()) {
        Logger::error(LogFacility::World) << "Could not create initmaps!" << Log::end;
    } else {
        unsigned short int size = maps.size();
        Logger::info(LogFacility::World) << "Saving " << size << " maps." << Log::end;
        mapinitfile.write((char *) & size, sizeof(size));
        char mname[200];

        for (auto mapI = maps.begin(); mapI != maps.end(); ++mapI) {
            mapinitfile.write((char *) & (*mapI)->Z_Level, sizeof((*mapI)->Z_Level));
            mapinitfile.write((char *) & (*mapI)->Min_X, sizeof((*mapI)->Min_X));
            mapinitfile.write((char *) & (*mapI)->Min_Y, sizeof((*mapI)->Min_Y));

            mapinitfile.write((char *) & (*mapI)->Width, sizeof((*mapI)->Width));
            mapinitfile.write((char *) & (*mapI)->Height, sizeof((*mapI)->Height));

            sprintf(mname, "%s_%6d_%6d_%6d", prefix.c_str(), (*mapI)->Z_Level, (*mapI)->Min_X, (*mapI)->Min_Y);
            (*mapI)->Save(mname);
        }

        mapinitfile.close();
    }
}

