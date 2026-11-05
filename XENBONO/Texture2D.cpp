#include "Texture2D.h"

#include "xenbono.h"

UClass* UTexture2D::StaticClass()
{
	static auto Class = FindObject<UClass>(L"/Script/Engine.Texture2D");
	return Class;
}