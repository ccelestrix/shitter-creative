#include "Package.h"

#include "xenbono.h"

UClass* UPackage::StaticClass()
{
	static auto Class = FindObject<UClass>(L"/Script/CoreUObject.Package");
	return Class;
}
