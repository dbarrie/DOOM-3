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
#include "BufferObject.h"
#include "RenderBackend.h"

idCVar r_showBuffers( "r_showBuffers", "0", CVAR_INTEGER, "" );

#define assert_16_byte_aligned( ptr )		assert( ( ((UINT_PTR)(ptr)) & 15 ) == 0 )

/*
==================
IsWriteCombined
==================
*/
bool IsWriteCombined( void * base ) {
	MEMORY_BASIC_INFORMATION info;
	SIZE_T size = VirtualQueryEx( GetCurrentProcess(), base, &info, sizeof( info ) );
	if ( size == 0 ) {
		DWORD error = GetLastError();
		error = error;
		return false;
	}
	bool isWriteCombined = ( ( info.AllocationProtect & PAGE_WRITECOMBINE ) != 0 );
	return isWriteCombined;
}

#ifdef ID_WIN_X86_SSE2_INTRIN

/*
========================
CopyBuffer
========================
*/
void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	assert_16_byte_aligned( dst );
	assert_16_byte_aligned( src );

	int i = 0;
	for ( ; i + 128 <= numBytes; i += 128 ) {
		__m128i d0 = _mm_load_si128( (__m128i *)&src[i + 0*16] );
		__m128i d1 = _mm_load_si128( (__m128i *)&src[i + 1*16] );
		__m128i d2 = _mm_load_si128( (__m128i *)&src[i + 2*16] );
		__m128i d3 = _mm_load_si128( (__m128i *)&src[i + 3*16] );
		__m128i d4 = _mm_load_si128( (__m128i *)&src[i + 4*16] );
		__m128i d5 = _mm_load_si128( (__m128i *)&src[i + 5*16] );
		__m128i d6 = _mm_load_si128( (__m128i *)&src[i + 6*16] );
		__m128i d7 = _mm_load_si128( (__m128i *)&src[i + 7*16] );
		_mm_stream_si128( (__m128i *)&dst[i + 0*16], d0 );
		_mm_stream_si128( (__m128i *)&dst[i + 1*16], d1 );
		_mm_stream_si128( (__m128i *)&dst[i + 2*16], d2 );
		_mm_stream_si128( (__m128i *)&dst[i + 3*16], d3 );
		_mm_stream_si128( (__m128i *)&dst[i + 4*16], d4 );
		_mm_stream_si128( (__m128i *)&dst[i + 5*16], d5 );
		_mm_stream_si128( (__m128i *)&dst[i + 6*16], d6 );
		_mm_stream_si128( (__m128i *)&dst[i + 7*16], d7 );
	}
	for ( ; i + 16 <= numBytes; i += 16 ) {
		__m128i d = _mm_load_si128( (__m128i *)&src[i] );
		_mm_stream_si128( (__m128i *)&dst[i], d );
	}
	for ( ; i + 4 <= numBytes; i += 4 ) {
		*(uint32 *)&dst[i] = *(const uint32 *)&src[i];
	}
	for ( ; i < numBytes; i++ ) {
		dst[i] = src[i];
	}
	_mm_sfence();
}

#else

/*
========================
CopyBuffer
========================
*/
void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	//assert_16_byte_aligned( dst );
	//assert_16_byte_aligned( src );
	memcpy( dst, src, numBytes );
}

#endif

/*
================================================================================================

	idBufferObject

================================================================================================
*/

/*
========================
idBufferObject::idBufferObject
========================
*/
idBufferObject::idBufferObject() {
	m_size = 0;
	m_offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	m_usage = BU_STATIC;
	m_apiObject = FGL_NULL_HANDLE;
	m_memory = FGL_NULL_HANDLE;
}

/*
========================
idBufferObject::ClearWithoutFreeing
========================
*/
void idBufferObject::ClearWithoutFreeing() {
	m_size = 0;
	m_offsetInOtherBuffer = OWNS_BUFFER_FLAG;
	m_apiObject = FGL_NULL_HANDLE;
	m_memory = FGL_NULL_HANDLE;
}

/*
================================================================================================

	idVertexBuffer

================================================================================================
*/

/*
========================
idVertexBuffer::idVertexBuffer
========================
*/
idVertexBuffer::idVertexBuffer() {
	SetUnmapped();
}

/*
========================
idVertexBuffer::~idVertexBuffer
========================
*/
idVertexBuffer::~idVertexBuffer() {
	FreeBufferObject();
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idVertexBuffer & other ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( other.GetSize() > 0 );

	FreeBufferObject();
	m_size = other.GetSize();					// this strips the MAPPED_FLAG
	m_offsetInOtherBuffer = other.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idVertexBuffer & other, int refOffset, int refSize ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= other.GetSize() );

	FreeBufferObject();
	m_size = refSize;
	m_offsetInOtherBuffer = other.GetOffset() + refOffset;
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}



/*
========================
idVertexBuffer::AllocBufferObject
========================
*/
bool idVertexBuffer::AllocBufferObject(const void* data, int allocSize, bufferUsageType_t usage) {
	assert(m_apiObject == FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);

	if (allocSize <= 0) {
		common->Error("idVertexBuffer::AllocBufferObject: allocSize = %i", allocSize);
	}

	m_size = allocSize;
	m_usage = usage;

	bool allocationFailed = false;

	int numBytes = GetAllocedSize();

	FglBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.size = numBytes;
	/*
	bufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (m_usage == BU_STATIC) {
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	*/

	ID_FGL_CHECK(fglCreateBuffer(fglcontext.device, &bufferCreateInfo, nullptr, &m_apiObject));

	FglMemoryRequirements memoryRequirements;
	fglGetBufferMemoryRequirements(fglcontext.device, m_apiObject, &memoryRequirements);
	ID_FGL_CHECK(fglAllocateMemory(fglcontext.device, memoryRequirements.size, nullptr, &m_memory));

	ID_FGL_CHECK(fglBindBufferMemory(fglcontext.device, m_apiObject, m_memory, 0));

	if (r_showBuffers.GetBool()) {
		common->Printf("vertex buffer alloc %p, (%i bytes)\n", this, GetSize());
	}

	// copy the data
	if (data != NULL) {
		Update(data, allocSize);
	}

	return !allocationFailed;
}

/*
========================
idVertexBuffer::FreeBufferObject
========================
*/
void idVertexBuffer::FreeBufferObject() {
	if (IsMapped()) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if (OwnsBuffer() == false) {
		ClearWithoutFreeing();
		return;
	}

	if (m_apiObject == FGL_NULL_HANDLE) {
		return;
	}

	if (r_showBuffers.GetBool()) {
		common->Printf("vertex buffer free %p, (%i bytes)\n", this, GetSize());
	}

	fglDestroyBuffer(fglcontext.device, m_apiObject, nullptr);
	fglFreeMemory(fglcontext.device, m_memory, nullptr);
	m_apiObject = FGL_NULL_HANDLE;
	m_memory = FGL_NULL_HANDLE;

	ClearWithoutFreeing();
}

/*
========================
idVertexBuffer::Update
========================
*/
void idVertexBuffer::Update(const void* data, int size, int offset) const {
	assert(m_apiObject != FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);
	assert((GetOffset() & 15) == 0);

	if (size > GetSize()) {
		common->FatalError("idVertexBuffer::Update: size overrun, %i > %i\n", size, GetSize());
	}

	void* pData = nullptr;
	fglMapMemory(fglcontext.device, m_memory, offset, size, &pData);
	CopyBuffer((byte*)pData, (const byte*)data, size);
	fglUnmapMemory(fglcontext.device, m_memory);
}

/*
========================
idVertexBuffer::MapBuffer
========================
*/
void* idVertexBuffer::MapBuffer(bufferMapType_t mapType) {
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idVertexBuffer::MapBuffer: Cannot map a buffer marked as BU_STATIC.");
	}

	void* buffer = nullptr;
	fglMapMemory(fglcontext.device, m_memory, GetOffset(), GetSize(), &buffer);

	SetMapped();

	if (buffer == NULL) {
		common->FatalError("idVertexBuffer::MapBuffer: failed");
	}
	return buffer;
}

/*
========================
idVertexBuffer::UnmapBuffer
========================
*/
void idVertexBuffer::UnmapBuffer() {
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idVertexBuffer::UnmapBuffer: Cannot unmap a buffer marked as BU_STATIC.");
	}

	fglUnmapMemory(fglcontext.device, m_memory);

	SetUnmapped();
}



/*
================================================================================================

idIndexBuffer

================================================================================================
*/

/*
========================
idIndexBuffer::idIndexBuffer
========================
*/
idIndexBuffer::idIndexBuffer() {
	SetUnmapped();
}


/*
========================
idIndexBuffer::~idIndexBuffer
========================
*/
idIndexBuffer::~idIndexBuffer() {
	FreeBufferObject();
}

/*
========================
idIndexBuffer::AllocBufferObject
========================
*/
bool idIndexBuffer::AllocBufferObject(const void* data, int allocSize, bufferUsageType_t usage) {
	assert(m_apiObject == FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);

	if (allocSize <= 0) {
		common->Error("idIndexBuffer::AllocBufferObject: allocSize = %i", allocSize);
	}

	m_size = allocSize;
	m_usage = usage;

	bool allocationFailed = false;

	int numBytes = GetAllocedSize();

	FglBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.size = numBytes;
	/*
	bufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (m_usage == BU_STATIC) {
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	*/

	ID_FGL_CHECK(fglCreateBuffer(fglcontext.device, &bufferCreateInfo, nullptr, &m_apiObject));

	FglMemoryRequirements memoryRequirements;
	fglGetBufferMemoryRequirements(fglcontext.device, m_apiObject, &memoryRequirements);
	ID_FGL_CHECK(fglAllocateMemory(fglcontext.device, memoryRequirements.size, nullptr, &m_memory));

	ID_FGL_CHECK(fglBindBufferMemory(fglcontext.device, m_apiObject, m_memory, 0));


	if (r_showBuffers.GetBool()) {
		common->Printf("index buffer alloc %p, (%i bytes)\n", this, GetSize());
	}

	// copy the data
	if (data != NULL) {
		Update(data, allocSize);
	}

	return !allocationFailed;
}

/*
========================
idIndexBuffer::FreeBufferObject
========================
*/
void idIndexBuffer::FreeBufferObject() {
	if (IsMapped()) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if (OwnsBuffer() == false) {
		ClearWithoutFreeing();
		return;
	}

	if (m_apiObject == FGL_NULL_HANDLE) {
		return;
	}

	if (r_showBuffers.GetBool()) {
		common->Printf("index buffer free %p, (%i bytes)\n", this, GetSize());
	}

	fglDestroyBuffer(fglcontext.device, m_apiObject, nullptr);
	fglFreeMemory(fglcontext.device, m_memory, nullptr);
	m_apiObject = FGL_NULL_HANDLE;
	m_memory = FGL_NULL_HANDLE;

	ClearWithoutFreeing();
}

/*
========================
idIndexBuffer::Update
========================
*/
void idIndexBuffer::Update(const void* data, int size, int offset) const {
	assert(m_apiObject != FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);
	assert((GetOffset() & 15) == 0);

	if (size > GetSize()) {
		common->FatalError("idIndexBuffer::Update: size overrun, %i > %i\n", size, GetSize());
	}

	void* pData = nullptr;
	fglMapMemory(fglcontext.device, m_memory, offset, size, &pData);
	CopyBuffer((byte*)pData, (const byte*)data, size);
	fglUnmapMemory(fglcontext.device, m_memory);
}

/*
========================
idIndexBuffer::MapBuffer
========================
*/
void* idIndexBuffer::MapBuffer(bufferMapType_t mapType) {
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idIndexBuffer::MapBuffer: Cannot map a buffer marked as BU_STATIC.");
	}

	void* buffer = nullptr;
	fglMapMemory(fglcontext.device, m_memory, GetOffset(), GetSize(), &buffer);

	SetMapped();

	if (buffer == NULL) {
		common->FatalError("idIndexBuffer::MapBuffer: failed");
	}
	return buffer;
}

/*
========================
idIndexBuffer::UnmapBuffer
========================
*/
void idIndexBuffer::UnmapBuffer() {
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idIndexBuffer::UnmapBuffer: Cannot unmap a buffer marked as BU_STATIC.");
	}

	fglUnmapMemory(fglcontext.device, m_memory);

	SetUnmapped();
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idIndexBuffer & other ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( other.GetSize() > 0 );

	FreeBufferObject();
	m_size = other.GetSize();					// this strips the MAPPED_FLAG
	m_offsetInOtherBuffer = other.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idIndexBuffer & other, int refOffset, int refSize ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= other.GetSize() );

	FreeBufferObject();
	m_size = refSize;
	m_offsetInOtherBuffer = other.GetOffset() + refOffset;
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}

/*
================================================================================================

idUniformBuffer

================================================================================================
*/

/*
========================
idUniformBuffer::idUniformBuffer
========================
*/
idUniformBuffer::idUniformBuffer() {
	m_usage = BU_DYNAMIC;
	SetUnmapped();
}

/*
========================
idUniformBuffer::~idUniformBuffer
========================
*/
idUniformBuffer::~idUniformBuffer() {
	FreeBufferObject();
}


/*
========================
idUniformBuffer::AllocBufferObject
========================
*/
bool idUniformBuffer::AllocBufferObject(const void* data, int allocSize, bufferUsageType_t usage) {
	assert(m_apiObject == FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);

	if (allocSize <= 0) {
		idLib::Error("idUniformBuffer::AllocBufferObject: allocSize = %i", allocSize);
	}

	m_size = allocSize;
	m_usage = usage;

	bool allocationFailed = false;

	const int numBytes = GetAllocedSize();

	FglBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.size = numBytes;
	/*
	bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORMBUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (m_usage == BU_STATIC) {
		bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	*/

	ID_FGL_CHECK(fglCreateBuffer(fglcontext.device, &bufferCreateInfo, nullptr, &m_apiObject));

	FglMemoryRequirements memoryRequirements;
	fglGetBufferMemoryRequirements(fglcontext.device, m_apiObject, &memoryRequirements);
	ID_FGL_CHECK(fglAllocateMemory(fglcontext.device, memoryRequirements.size, nullptr, &m_memory));

	ID_FGL_CHECK(fglBindBufferMemory(fglcontext.device, m_apiObject, m_memory, 0));

	if (r_showBuffers.GetBool()) {
		common->Printf("joint buffer alloc %p, (%i bytes)\n", this, GetSize());
	}

	// copy the data
	if (data != NULL) {
		Update(data, allocSize);
	}

	return !allocationFailed;
}

/*
========================
idUniformBuffer::FreeBufferObject
========================
*/
void idUniformBuffer::FreeBufferObject() {
	if (IsMapped()) {
		UnmapBuffer();
	}

	// if this is a sub-allocation inside a larger buffer, don't actually free anything.
	if (OwnsBuffer() == false) {
		ClearWithoutFreeing();
		return;
	}

	if (m_apiObject == FGL_NULL_HANDLE) {
		return;
	}

	if (r_showBuffers.GetBool()) {
		common->Printf("joint buffer free %p, (%i bytes)\n", this, GetSize());
	}

	fglDestroyBuffer(fglcontext.device, m_apiObject, nullptr);
	fglFreeMemory(fglcontext.device, m_memory, nullptr);
	m_apiObject = FGL_NULL_HANDLE;
	m_memory = FGL_NULL_HANDLE;

	ClearWithoutFreeing();
}

/*
========================
idUniformBuffer::Update
========================
*/
void idUniformBuffer::Update(const void* data, int size, int offset) const {
	assert(m_apiObject != FGL_NULL_HANDLE);
	//assert_16_byte_aligned(data);
	assert((GetOffset() & 15) == 0);

	if (size > GetSize()) {
		common->FatalError("idUniformBuffer::Update: size overrun, %i > %i\n", size, m_size);
	}

	void* pData = nullptr;
	fglMapMemory(fglcontext.device, m_memory, offset, size, &pData);
	CopyBuffer((byte*)pData, (const byte*)data, size);
	fglUnmapMemory(fglcontext.device, m_memory);
}

/*
========================
idUniformBuffer::MapBuffer
========================
*/
void* idUniformBuffer::MapBuffer(bufferMapType_t mapType) {
	assert(mapType == BM_WRITE);
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idUniformBuffer::MapBuffer: Cannot map a buffer marked as BU_STATIC.");
	}

	void* buffer = nullptr;
	fglMapMemory(fglcontext.device, m_memory, GetOffset(), GetSize(), &buffer);

	SetMapped();

	if (buffer == NULL) {
		common->FatalError("idUniformBuffer::MapBuffer: failed");
	}
	return buffer;
}

/*
========================
idUniformBuffer::UnmapBuffer
========================
*/
void idUniformBuffer::UnmapBuffer() {
	assert(m_apiObject != FGL_NULL_HANDLE);

	if (m_usage == BU_STATIC) {
		common->FatalError("idUniformBuffer::UnmapBuffer: Cannot unmap a buffer marked as BU_STATIC.");
	}

	fglUnmapMemory(fglcontext.device, m_memory);

	SetUnmapped();
}


/*
========================
idUniformBuffer::Reference
========================
*/
void idUniformBuffer::Reference( const idUniformBuffer & other ) {
	assert( IsMapped() == false );
	assert( other.IsMapped() == false );
	assert( other.GetSize() > 0 );

	FreeBufferObject();
	m_size = other.GetSize();					// this strips the MAPPED_FLAG
	m_offsetInOtherBuffer = other.GetOffset();	// this strips the OWNS_BUFFER_FLAG
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}

/*
========================
idUniformBuffer::Reference
========================
*/
void idUniformBuffer::Reference( const idUniformBuffer & other, int refOffset, int refSize ) {
	assert( IsMapped() == false );
	assert( other.IsMapped() == false );
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= other.GetSize() );

	FreeBufferObject();
	m_size = refSize;
	m_offsetInOtherBuffer = other.GetOffset() + refOffset;
	m_usage = other.m_usage;
	m_apiObject = other.m_apiObject;
	m_memory = other.m_memory;

	assert( OwnsBuffer() == false );
}