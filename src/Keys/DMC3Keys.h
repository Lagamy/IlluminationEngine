#pragma once
#include <cstdint>

namespace DMC3
{
/*
A model of the Devil May Cry HD Collection (DMC3): the `.nbz` archive it is in (a path under dmc3DataPath, Constants.h, e.g. "dmc3-0.nbz"), the `.pac` file inside it ("GData.afs/pl000.pac") and the slot of the pac's
top level that holds the `MOD ` entry and the slot that holds its textures (see docs/DMC3_FORMATS.md). Imported by importMod the first time an instance needs it; its DMC3Mesh is named "<archive>:<pac>#<slot>".
*/
struct ModelKey
{
	const char* archive;
	const char* pac;
	uint32_t slot;
	uint32_t textureSlot;
};

/* A motion of the same archives: the `.pac` file and the slot of its top level that holds the `MOT\0` entry (docs/DMC3_FORMATS.md, "MOT (motion)"). Its Motion is named "<archive>:<pac>#<slot>". */
struct MotionKey
{
	const char* archive;
	const char* pac;
	uint32_t slot;
};
}
