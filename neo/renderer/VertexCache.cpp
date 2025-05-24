/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2016-2017 Dustin Land

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "../idlib/precompiled.h"


#include "VertexCache.h"

idVertexCache vertexCache;

idCVar r_showVertexCache("r_showVertexCache", "0", CVAR_RENDERER | CVAR_BOOL, "Print stats about the vertex cache every frame");
idCVar r_showVertexCacheTimings("r_showVertexCache", "0", CVAR_RENDERER | CVAR_BOOL, "Print stats about the vertex cache every frame");


/*
==============
ClearGeoBufferSet
==============
*/
static void ClearGeoBufferSet(geoBufferSet_t& gbs) {
	gbs.indexMemUsed = 0;
	gbs.vertexMemUsed = 0;
	gbs.jointMemUsed = 0;
	gbs.allocations = 0;
}

/*
==============
MapGeoBufferSet
==============
*/
static void MapGeoBufferSet(geoBufferSet_t& gbs) {
	if (gbs.mappedVertexBase == NULL) {
		gbs.mappedVertexBase = (byte*)gbs.vertexBuffer.MapBuffer(BM_WRITE);
	}
	if (gbs.mappedIndexBase == NULL) {
		gbs.mappedIndexBase = (byte*)gbs.indexBuffer.MapBuffer(BM_WRITE);
	}
	if (gbs.mappedJointBase == NULL && gbs.jointBuffer.GetAllocedSize() != 0) {
		gbs.mappedJointBase = (byte*)gbs.jointBuffer.MapBuffer(BM_WRITE);
	}
}

/*
==============
UnmapGeoBufferSet
==============
*/
static void UnmapGeoBufferSet(geoBufferSet_t& gbs) {
	if (gbs.mappedVertexBase != NULL) {
		gbs.vertexBuffer.UnmapBuffer();
		gbs.mappedVertexBase = NULL;
	}
	if (gbs.mappedIndexBase != NULL) {
		gbs.indexBuffer.UnmapBuffer();
		gbs.mappedIndexBase = NULL;
	}
	if (gbs.mappedJointBase != NULL) {
		gbs.jointBuffer.UnmapBuffer();
		gbs.mappedJointBase = NULL;
	}
}

/*
==============
AllocGeoBufferSet
==============
*/
static void AllocGeoBufferSet(geoBufferSet_t& gbs, const int vertexBytes, const int indexBytes, const int jointBytes, bufferUsageType_t usage) {
	gbs.vertexBuffer.AllocBufferObject(NULL, vertexBytes, usage);
	gbs.indexBuffer.AllocBufferObject(NULL, indexBytes, usage);
	if (jointBytes > 0) {
		gbs.jointBuffer.AllocBufferObject(NULL, jointBytes, usage);
	}

	ClearGeoBufferSet(gbs);
}

/*
==============
idVertexCache::Init
==============
*/
void idVertexCache::Init(int uniformBufferOffsetAlignment) {
	m_currentFrame = 0;
	m_listNum = 0;

	m_uniformBufferOffsetAlignment = uniformBufferOffsetAlignment;

	m_mostUsedVertex = 0;
	m_mostUsedIndex = 0;
	m_mostUsedJoint = 0;

	for (int i = 0; i < NUM_FRAME_DATA; i++) {
		AllocGeoBufferSet(m_frameData[i], VERTCACHE_VERTEX_MEMORY_PER_FRAME, VERTCACHE_INDEX_MEMORY_PER_FRAME, VERTCACHE_JOINT_MEMORY_PER_FRAME, BU_DYNAMIC);
	}
#if 1
	AllocGeoBufferSet(m_staticData, STATIC_VERTEX_MEMORY, STATIC_INDEX_MEMORY, 0, BU_STATIC);
#else
	AllocGeoBufferSet(m_staticData, STATIC_VERTEX_MEMORY, STATIC_INDEX_MEMORY, 0, BU_DYNAMIC);
#endif

	//MapGeoBufferSet(m_frameData[m_listNum]);
}

/*
==============
idVertexCache::Shutdown
==============
*/
void idVertexCache::Shutdown() {
	for (int i = 0; i < NUM_FRAME_DATA; i++) {
		m_frameData[i].vertexBuffer.FreeBufferObject();
		m_frameData[i].indexBuffer.FreeBufferObject();
		m_frameData[i].jointBuffer.FreeBufferObject();
	}
}

/*
==============
idVertexCache::PurgeAll
==============
*/
void idVertexCache::PurgeAll() {
	Shutdown();
	Init(m_uniformBufferOffsetAlignment);
}

/*
==============
idVertexCache::FreeStaticData

call on loading a new map
==============
*/
void idVertexCache::FreeStaticData() {
	ClearGeoBufferSet(m_staticData);
	m_mostUsedVertex = 0;
	m_mostUsedIndex = 0;
	m_mostUsedJoint = 0;
}

/*
==============
idVertexCache::ActuallyAlloc
==============
*/
vertCacheHandle_t idVertexCache::ActuallyAlloc(geoBufferSet_t& vcs, const void* data, int bytes, cacheType_t type) {
	if (bytes == 0) {
		return (vertCacheHandle_t)0;
	}

	//assert((((UINT_PTR)(data)) & 15) == 0);
	//assert((bytes & 15) == 0);

	int	endPos = 0;
	int offset = 0;

	switch (type) {
	case CACHE_INDEX: {
		endPos = vcs.indexMemUsed += bytes;
		if (endPos > vcs.indexBuffer.GetAllocedSize()) {
			idLib::Error("Out of index cache");
		}

		offset = endPos - bytes;

		if (data != NULL) {
			if (vcs.indexBuffer.GetUsage() == BU_DYNAMIC) {
				//MapGeoBufferSet(vcs);
			}
			vcs.indexBuffer.Update(data, bytes, offset);
		}

		break;
	}
	case CACHE_VERTEX: {
		endPos = vcs.vertexMemUsed += bytes;
		if (endPos > vcs.vertexBuffer.GetAllocedSize()) {
			idLib::Error("Out of vertex cache");
		}

		offset = endPos - bytes;

		if (data != NULL) {
			if (vcs.vertexBuffer.GetUsage() == BU_DYNAMIC) {
				//MapGeoBufferSet(vcs);
			}
			vcs.vertexBuffer.Update(data, bytes, offset);
		}

		break;
	}
	case CACHE_JOINT: {
		endPos = vcs.jointMemUsed += bytes;
		if (endPos > vcs.jointBuffer.GetAllocedSize()) {
			idLib::Error("Out of joint buffer cache");
		}

		offset = endPos - bytes;

		if (data != NULL) {
			if (vcs.jointBuffer.GetUsage() == BU_DYNAMIC) {
				//MapGeoBufferSet(vcs);
			}
			vcs.jointBuffer.Update(data, bytes, offset);
		}

		break;
	}
	default:
		assert(false);
	}

	vcs.allocations++;

	vertCacheHandle_t handle = ((uint64_t)(m_currentFrame & VERTCACHE_FRAME_MASK) << VERTCACHE_FRAME_SHIFT) |
		((uint64_t)(offset & VERTCACHE_OFFSET_MASK) << VERTCACHE_OFFSET_SHIFT) |
		((uint64_t)(bytes & VERTCACHE_SIZE_MASK) << VERTCACHE_SIZE_SHIFT);
	if (&vcs == &m_staticData) {
		handle |= VERTCACHE_STATIC;
	}
	return handle;
}

static int ALIGN(int size, int align)
{
	return (size + align - 1) & ~(align - 1);
}

/*
==============
idVertexCache::AllocVertex
==============
*/
vertCacheHandle_t idVertexCache::AllocVertex(const void* data, int num, size_t size /*= sizeof( idDrawVert ) */) {
	return ActuallyAlloc(m_frameData[m_listNum], data, ALIGN(num * (int)size, VERTEX_CACHE_ALIGN), CACHE_VERTEX);
}

/*
==============
idVertexCache::AllocIndex
==============
*/
vertCacheHandle_t idVertexCache::AllocIndex(const void* data, int num, size_t size /*= sizeof( triIndex_t ) */) {
	return ActuallyAlloc(m_frameData[m_listNum], data, ALIGN(num * (int)size, INDEX_CACHE_ALIGN), CACHE_INDEX);
}

/*
==============
idVertexCache::AllocJoint
==============
*/
vertCacheHandle_t idVertexCache::AllocJoint(const void* data, int num, size_t size /*= sizeof( idJointMat ) */) {
	return ActuallyAlloc(m_frameData[m_listNum], data, ALIGN(num * (int)size, m_uniformBufferOffsetAlignment), CACHE_JOINT);
}

/*
==============
idVertexCache::AllocStaticVertex
==============
*/
vertCacheHandle_t idVertexCache::AllocStaticVertex(const void* data, int bytes) {
	if (m_staticData.vertexMemUsed + bytes > STATIC_VERTEX_MEMORY) {
		common->FatalError("AllocStaticVertex failed, increase STATIC_VERTEX_MEMORY");
	}
	return ActuallyAlloc(m_staticData, data, bytes, CACHE_VERTEX);
}

/*
==============
idVertexCache::AllocStaticIndex
==============
*/
vertCacheHandle_t idVertexCache::AllocStaticIndex(const void* data, int bytes) {
	if (m_staticData.indexMemUsed + bytes > STATIC_INDEX_MEMORY) {
		common->FatalError("AllocStaticIndex failed, increase STATIC_INDEX_MEMORY");
	}
	return ActuallyAlloc(m_staticData, data, bytes, CACHE_INDEX);
}

/*
==============
idVertexCache::MappedVertexBuffer
==============
*/
byte* idVertexCache::MappedVertexBuffer(vertCacheHandle_t handle) {
	//release_assert(!CacheIsStatic(handle));
	const uint64_t offset = (int)(handle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	//release_assert(frameNum == (m_currentFrame & VERTCACHE_FRAME_MASK));
	return m_frameData[m_listNum].mappedVertexBase + offset;
}

/*
==============
idVertexCache::MappedIndexBuffer
==============
*/
byte* idVertexCache::MappedIndexBuffer(vertCacheHandle_t handle) {
	//release_assert(!CacheIsStatic(handle));
	const uint64_t offset = (int)(handle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	//release_assert(frameNum == (m_currentFrame & VERTCACHE_FRAME_MASK));
	return m_frameData[m_listNum].mappedIndexBase + offset;
}

/*
==============
idVertexCache::CacheIsCurrent
==============
*/
bool idVertexCache::CacheIsCurrent(const vertCacheHandle_t handle) {
	const int isStatic = handle & VERTCACHE_STATIC;
	if (isStatic) {
		return true;
	}
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	if (frameNum != (m_currentFrame & VERTCACHE_FRAME_MASK)) {
		return false;
	}
	return true;
}

/*
==============
idVertexCache::GetVertexBuffer
==============
*/
bool idVertexCache::GetVertexBuffer(vertCacheHandle_t handle, idVertexBuffer* vb) {
	const int isStatic = handle & VERTCACHE_STATIC;
	const uint64_t size = (int)(handle >> VERTCACHE_SIZE_SHIFT) & VERTCACHE_SIZE_MASK;
	const uint64_t offset = (int)(handle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	if (isStatic) {
		vb->Reference(m_staticData.vertexBuffer, offset, size);
		return true;
	}
	if (frameNum != ((m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
		return false;
	}
	vb->Reference(m_frameData[m_drawListNum].vertexBuffer, offset, size);
	return true;
}

/*
==============
idVertexCache::GetIndexBuffer
==============
*/
bool idVertexCache::GetIndexBuffer(vertCacheHandle_t handle, idIndexBuffer* ib) {
	const int isStatic = handle & VERTCACHE_STATIC;
	const uint64_t size = (int)(handle >> VERTCACHE_SIZE_SHIFT) & VERTCACHE_SIZE_MASK;
	const uint64_t offset = (int)(handle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	if (isStatic) {
		ib->Reference(m_staticData.indexBuffer, offset, size);
		return true;
	}
	if (frameNum != ((m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
		return false;
	}
	ib->Reference(m_frameData[m_drawListNum].indexBuffer, offset, size);
	return true;
}

/*
==============
idVertexCache::GetJointBuffer
==============
*/
bool idVertexCache::GetJointBuffer(vertCacheHandle_t handle, idUniformBuffer* jb) {
	const int isStatic = handle & VERTCACHE_STATIC;
	const uint64_t numBytes = (int)(handle >> VERTCACHE_SIZE_SHIFT) & VERTCACHE_SIZE_MASK;
	const uint64_t jointOffset = (int)(handle >> VERTCACHE_OFFSET_SHIFT) & VERTCACHE_OFFSET_MASK;
	const uint64_t frameNum = (int)(handle >> VERTCACHE_FRAME_SHIFT) & VERTCACHE_FRAME_MASK;
	if (isStatic) {
		jb->Reference(m_staticData.jointBuffer, jointOffset, numBytes);
		return true;
	}
	if (frameNum != ((m_currentFrame - 1) & VERTCACHE_FRAME_MASK)) {
		return false;
	}
	jb->Reference(m_frameData[m_drawListNum].jointBuffer, jointOffset, numBytes);
	return true;
}

/*
==============
idVertexCache::BeginBackEnd
==============
*/
void idVertexCache::BeginBackEnd() {
	m_mostUsedVertex = max(m_mostUsedVertex, m_frameData[m_listNum].vertexMemUsed);
	m_mostUsedIndex = max(m_mostUsedIndex, m_frameData[m_listNum].indexMemUsed);
	m_mostUsedJoint = max(m_mostUsedJoint, m_frameData[m_listNum].jointMemUsed);

	if (r_showVertexCache.GetBool()) {
		common->Printf("%08d: %d allocations, %dkB vertex, %dkB index, %kB joint : %dkB vertex, %dkB index, %kB joint\n",
			m_currentFrame, m_frameData[m_listNum].allocations,
			m_frameData[m_listNum].vertexMemUsed / 1024,
			m_frameData[m_listNum].indexMemUsed / 1024,
			m_frameData[m_listNum].jointMemUsed / 1024,
			m_mostUsedVertex / 1024,
			m_mostUsedIndex / 1024,
			m_mostUsedJoint / 1024);
	}

	// unmap the current frame so the GPU can read it
	const int startUnmap = Sys_Milliseconds();
	//UnmapGeoBufferSet(m_frameData[m_listNum]);
	//UnmapGeoBufferSet(m_staticData);
	const int endUnmap = Sys_Milliseconds();
	if (endUnmap - startUnmap > 1) {
		if (r_showVertexCacheTimings.GetBool())
			common->Printf("idVertexCache::unmap took %i msec\n", endUnmap - startUnmap);
	}
	m_drawListNum = m_listNum;

	// prepare the next frame for writing to by the CPU
	m_currentFrame++;

	m_listNum = m_currentFrame % NUM_FRAME_DATA;
	const int startMap = Sys_Milliseconds();
	//MapGeoBufferSet(m_frameData[m_listNum]);
	const int endMap = Sys_Milliseconds();
	if (endMap - startMap > 1) {
		if (r_showVertexCacheTimings.GetBool())
			common->Printf("idVertexCache::map took %i msec\n", endMap - startMap);
	}

	ClearGeoBufferSet(m_frameData[m_listNum]);
}

