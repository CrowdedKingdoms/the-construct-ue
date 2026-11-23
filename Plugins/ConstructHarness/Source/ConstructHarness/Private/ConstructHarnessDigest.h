#pragma once

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

namespace ConstructHarness
{
	/** Lower-case hex SHA-1 of a chunk's grid, the digest the Node checks compare with their own reads. */
	inline FString GridDigest(const TArray<uint8>& Grid)
	{
		FSHAHash Hash;
		FSHA1::HashBuffer(Grid.GetData(), Grid.Num(), Hash.Hash);
		return Hash.ToString().ToLower();
	}
}
