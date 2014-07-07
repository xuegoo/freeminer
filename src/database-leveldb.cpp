/*
database-leveldb.cpp
Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#if USE_LEVELDB
/*
LevelDB databases
*/

#include "database-leveldb.h"
#include "map.h"
#include "mapblock.h"
#include "serialization.h"
#include "main.h"
#include "settings.h"
#include "log.h"

#define ENSURE_STATUS_OK(s) \
	if (!s.ok()) { \
		throw FileNotGoodException(std::string("LevelDB error: ") + s.ToString()); \
	}

Database_LevelDB::Database_LevelDB(ServerMap *map, std::string savedir)
{
	m_database = new KeyValueStorage(savedir, "map");
	srvmap = map;
}

int Database_LevelDB::Initialized(void)
{
	return 1;
}

void Database_LevelDB::beginSave() {}
void Database_LevelDB::endSave() {}

void Database_LevelDB::saveBlock(MapBlock *block)
{
	DSTACK(__FUNCTION_NAME);
	/*
		Dummy blocks are not written
	*/
	if(!block || block->isDummy())
	{
		return;
	}

	// Format used for writing
	u8 version = SER_FMT_VER_HIGHEST_WRITE;
	// Get destination
	v3s16 p3d = block->getPos();

	/*
		[0] u8 serialization version
		[1] data
	*/

	std::ostringstream o(std::ios_base::binary);
	o.write((char*)&version, 1);
	// Write basic data
	block->serialize(o, version, true);
	// Write block to database
	std::string tmp = o.str();

	m_database->put(getBlockAsString(p3d), tmp);
	m_database->del(i64tos(getBlockAsInteger(p3d))); // delete old format

	// We just wrote it to the disk so clear modified flag
	block->resetModified();
}

MapBlock* Database_LevelDB::loadBlock(v3s16 blockpos)
{
	v2s16 p2d(blockpos.X, blockpos.Z);

	std::string datastr;

	auto ok = m_database->get(getBlockAsString(blockpos), datastr);

	if (!datastr.length()) {

	ok = m_database->get(i64tos(getBlockAsInteger(blockpos)), datastr);
	if (datastr.length() == 0 && ok) {
		errorstream << "Blank block data in database (datastr.length() == 0) ("
			<< blockpos.X << "," << blockpos.Y << "," << blockpos.Z << ")" << std::endl;

		if (g_settings->getBool("ignore_world_load_errors")) {
			errorstream << "Ignoring block load error. Duck and cover! "
				<< "(ignore_world_load_errors)" << std::endl;
		} else {
			throw SerializationError("Blank block data in database");
		}
		return NULL;
	}

	}

	if (ok) {
		try {
			std::istringstream is(datastr, std::ios_base::binary);
			u8 version = SER_FMT_VER_INVALID;
			is.read((char *)&version, 1);

			if (is.fail())
				throw SerializationError("ServerMap::loadBlock(): Failed"
					" to read MapBlock version");

			MapBlock *block = NULL;
			bool created_new = false;
			block = srvmap->getBlockNoCreateNoEx(blockpos);
			if (block == NULL)
			{
				block = srvmap->createBlankBlockNoInsert(blockpos);
				created_new = true;
			}

			// Read basic data
			block->deSerialize(is, version, true);

			// If it's a new block, insert it to the map
			if (created_new)
				srvmap->insertBlock(block);

			/*
				Save blocks loaded in old format in new format
			*/
			//if(version < SER_FMT_VER_HIGHEST || save_after_load)
			// Only save if asked to; no need to update version
			//if(save_after_load)
			//     	saveBlock(block);
			// We just loaded it from, so it's up-to-date.
			block->resetModified();
		}
		catch (SerializationError &e)
		{
			errorstream << "Invalid block data in database"
				<< " (" << blockpos.X << "," << blockpos.Y << "," << blockpos.Z
				<< ") (SerializationError): " << e.what() << std::endl;
			// TODO: Block should be marked as invalid in memory so that it is
			// not touched but the game can run

			if (g_settings->getBool("ignore_world_load_errors")) {
				errorstream << "Ignoring block load error. Duck and cover! "
					<< "(ignore_world_load_errors)" << std::endl;
			} else {
				throw SerializationError("Invalid block data in database");
				//assert(0);
			}
		}

		return srvmap->getBlockNoCreateNoEx(blockpos);  // should not be using this here
	}
	return NULL;
}

void Database_LevelDB::listAllLoadableBlocks(std::list<v3s16> &dst)
{
#if USE_LEVELDB
	auto it = m_database->new_iterator();
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		dst.push_back(getStringAsBlock(it->key().ToString()));
	}
	ENSURE_STATUS_OK(it->status());  // Check for any errors found during the scan
	delete it;
#endif
}

Database_LevelDB::~Database_LevelDB()
{
	delete m_database;
}
#endif
