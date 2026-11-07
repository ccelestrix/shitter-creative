#include "FortAthenaCreativePortal.h"

#include "FortPlayerPawn.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlaysetItemDefinition.h"
#include "SoftObjectPtr.h"
#include "FortInventory.h"
#include "hooking.h"
#include "GameplayStatics.h"
#include "KismetStringLibrary.h"
#include "BuildingSMActor.h"
#include "NetDriver.h"
#include "PrefabArchitect.h"
#include "FortPlayerStateAthena.h"
#include <algorithm>
#include <map>
#include <set>

static bool FindIslandStart(AFortVolume* Volume, FVector& OutLocation, FRotator& OutRotation);

void AFortAthenaCreativePortal::TeleportPlayerToLinkedVolumeHook(UObject* Context, FFrame& Stack, void* Ret)
{
	LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume called on portal {}", __int64(Context));

	auto Portal = (AFortAthenaCreativePortal*)Context; // Cast?

	if (!Portal)
		return TeleportPlayerToLinkedVolumeOriginal(Context, Stack, Ret);

	AFortPlayerPawn* PlayerPawn = nullptr;
	bool bUseSpawnTags;

	Stack.StepCompiledIn(&PlayerPawn);
	Stack.StepCompiledIn(&bUseSpawnTags);

	LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: pawn {} bUseSpawnTags {}", __int64(PlayerPawn), bUseSpawnTags);

	if (!PlayerPawn)
		return TeleportPlayerToLinkedVolumeOriginal(Context, Stack, Ret);

	auto LinkedVolume = Portal->GetLinkedVolume();

	LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: linked volume {}", __int64(LinkedVolume));

	if (!LinkedVolume)
		return TeleportPlayerToLinkedVolumeOriginal(Context, Stack, Ret);

	if (auto PlayerController = Cast<AFortPlayerControllerAthena>(PlayerPawn->GetController()))
	{
		CreativeIsland::GiveCreativePhone(PlayerController);

		PlayerController->GetCreativePlotLinkedVolume() = LinkedVolume;
		CreativeIsland::CallOnRep(PlayerController, "OnRep_CreativePlotLinkedVolume");
	}

	CreativeIsland::DumpState("before the teleport onto the island");

	// The island's own player start (a FortPlayerStartCreative inside the island) is where the player lands, as in the game.
	FVector StartLocation;
	FRotator StartRotation;

	if (FindIslandStart(LinkedVolume, StartLocation, StartRotation))
	{
		LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: sending the pawn to the island's player start at {} {} {}", StartLocation.X, StartLocation.Y, StartLocation.Z);
		PlayerPawn->TeleportTo(StartLocation, StartRotation);
		return TeleportPlayerToLinkedVolumeOriginal(Context, Stack, Ret);
	}

	auto Location = LinkedVolume->GetActorLocation();
	Location.Z = 10000; // no player start on the island: it is entered from the sky, like the Xenon server does
	LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: sending the pawn to {} {} {}", Location.X, Location.Y, Location.Z);
	PlayerPawn->TeleportTo(Location, FRotator());

	if (auto BeginSkydivingFn = PlayerPawn->FindFunction("BeginSkydiving"))
	{
		bool bFromBus = false;
		PlayerPawn->ProcessEvent(BeginSkydivingFn, &bFromBus);
		LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: the pawn starts skydiving onto the island");
	}

	return TeleportPlayerToLinkedVolumeOriginal(Context, Stack, Ret);
}

void AFortAthenaCreativePortal::TeleportPlayerHook(UObject* Context, FFrame& Stack, void* Ret)
{
	auto Portal = (AFortAthenaCreativePortal*)Context; // Cast?

	if (!Portal)
		return TeleportPlayerOriginal(Context, Stack, Ret);

	AFortPlayerPawn* PlayerPawn = nullptr;
	FRotator TeleportRotation;

	Stack.StepCompiledIn(&PlayerPawn);
	Stack.StepCompiledIn(&TeleportRotation);

	LOG_INFO(LogCreative, "TeleportPlayer called with pawn {}", __int64(PlayerPawn));

	if (!PlayerPawn)
		return TeleportPlayerOriginal(Context, Stack, Ret);

	static auto bReturnToCreativeHubOffset = Portal->GetOffset("bReturnToCreativeHub");
	auto bReturnToCreativeHub = Portal->Get<bool>(bReturnToCreativeHubOffset);

	LOG_INFO(LogCreative, "TeleportPlayer: bReturnToCreativeHub {}", bReturnToCreativeHub);

	if (bReturnToCreativeHub)
	{
		auto Controller = Cast<AFortPlayerControllerAthena>(PlayerPawn->GetController());

		if (!Controller)
			return TeleportPlayerOriginal(Context, Stack, Ret);

		AFortPlayerControllerAthena::ServerTeleportToPlaygroundLobbyIslandHook(Controller);
	}
	else
	{
		static auto TeleportLocationOffset = Portal->GetOffset("TeleportLocation");
		auto& TeleportLocation = Portal->Get<FVector>(TeleportLocationOffset);
		PlayerPawn->TeleportTo(TeleportLocation, TeleportRotation);
	}

	return TeleportPlayerOriginal(Context, Stack, Ret);
}


// ---------------------------------------------------------------------------------------------------------------------
// CreativeIsland
// ---------------------------------------------------------------------------------------------------------------------
void CreativeIsland::CallOnRep(UObject* Object, const std::string& FunctionName)
{
	if (!Object)
	{
		LOG_WARN(LogCreative, "CallOnRep({}) on a null object", FunctionName);
		return;
	}

	auto Function = Object->FindFunction(FunctionName);

	if (!Function)
	{
		LOG_WARN(LogCreative, "{} has no function {} on this version, skipped", Object->GetName(), FunctionName);
		return;
	}

	Object->ProcessEvent(Function);
	LOG_INFO(LogCreative, "Called {} on {}", FunctionName, Object->GetName());
}

UObject* CreativeIsland::FindPlot()
{
	static auto Plot = FindObject<UObject>(L"/Game/Playgrounds/Items/Plots/Temperate_Medium.Temperate_Medium");

	if (!Plot)
		Plot = FindObject<UObject>(L"/CR_Legacy/Playgrounds/Items/Plots/Temperate_Medium.Temperate_Medium");

	LOG_INFO(LogCreative, "Island plot (Temperate_Medium): {}", Plot ? "found" : "NOT FOUND");
	return Plot;
}

UFortPlaysetItemDefinition* CreativeIsland::GetPlotPlayset(UObject* Plot)
{
	if (!Plot)
		return nullptr;

	static auto BasePlaysetOffset = Plot->GetOffset("BasePlayset", false);

	if (BasePlaysetOffset == -1)
	{
		LOG_WARN(LogCreative, "The plot {} has no BasePlayset", Plot->GetName());
		return nullptr;
	}

	auto& SoftPlayset = Plot->Get<TSoftObjectPtr<UFortPlaysetItemDefinition>>(BasePlaysetOffset);
	auto Playset = SoftPlayset.Get();

	if (!Playset)
		Playset = SoftPlayset.Get(FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetItemDefinition"), true);

	LOG_INFO(LogCreative, "Plot {} starts from playset {}", Plot->GetName(), Playset ? Playset->GetName() : std::string("NOT FOUND"));
	return Playset;
}

// The phone. The item goes into the inventory and the client is told to build its phone (ClientCreativePhoneCreated), but the client can only build it
// from an item it already has: the item is replicated with the next net update, after the call, and the player is also moved onto the island at that moment,
// so the phone never showed. The grant is therefore checked again and the client told again a few times in the seconds after it (TickPhone, from the driver's tick).
struct PhoneKick
{
	ULONGLONG Next = 0;
	int Left = 0;
	int Attempt = 0;
};

static std::map<UObject*, PhoneKick> PhoneKicks;
static void CheckCreativeHud(AFortPlayerControllerAthena* Controller, const char* When, bool bFix);

static UFortItemDefinition* CreativePhoneItem()
{
	static auto CreativePhone = FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/Prototype/WID_CreativeTool.WID_CreativeTool");
	return CreativePhone;
}

static bool PutPhoneInInventory(AFortPlayerControllerAthena* Controller)
{
	auto CreativePhone = CreativePhoneItem();
	auto WorldInventory = Controller ? Controller->GetWorldInventory() : nullptr;

	if (!CreativePhone || !WorldInventory)
		return false;

	if (WorldInventory->FindItemInstance(CreativePhone))
		return true;

	bool bShouldUpdate = false;
	WorldInventory->AddItem(CreativePhone, &bShouldUpdate, 1);

	if (bShouldUpdate)
		WorldInventory->Update(Controller);

	return WorldInventory->FindItemInstance(CreativePhone) != nullptr;
}

static void TickPhone()
{
	if (PhoneKicks.empty())
		return;

	const ULONGLONG Now = GetTickCount64();

	for (auto It = PhoneKicks.begin(); It != PhoneKicks.end();)
	{
		auto Controller = (AFortPlayerControllerAthena*)It->first;

		if (!Controller || !Controller->IsValidLowLevel())
		{
			It = PhoneKicks.erase(It);
			continue;
		}

		if (Now >= It->second.Next)
		{
			++It->second.Attempt;
			const bool bHad = Controller->GetWorldInventory() && Controller->GetWorldInventory()->FindItemInstance(CreativePhoneItem());
			const bool bHas = PutPhoneInInventory(Controller);

			LOG_INFO(LogCreative, "Creative phone for {}: {} in the inventory ({}), telling the client again (attempt {})", Controller->GetName(), bHas ? "it is" : "it is NOT", bHad ? "it was already" : "it was put there now",
				It->second.Attempt);
			CheckCreativeHud(Controller, "phone kick", true);
			CreativeIsland::CallOnRep(Controller, "ClientCreativePhoneCreated");

			if (--It->second.Left <= 0)
			{
				It = PhoneKicks.erase(It);
				continue;
			}

			It->second.Next = Now + 3500;
		}

		++It;
	}
}

void CreativeIsland::GiveCreativePhone(AFortPlayerControllerAthena* Controller)
{
	if (!Controller)
		return;

	if (!CreativePhoneItem())
	{
		LOG_WARN(LogCreative, "WID_CreativeTool was not found, so {} gets no creative phone", Controller->GetName());
		return;
	}

	if (!Controller->GetWorldInventory())
		return;

	const bool bHad = Controller->GetWorldInventory()->FindItemInstance(CreativePhoneItem()) != nullptr;
	const bool bHas = PutPhoneInInventory(Controller);

	LOG_INFO(LogCreative, "Gave {} the creative phone ({}, now {})", Controller->GetName(), bHad ? "it had it already" : "it did not have it", bHas ? "in the inventory" : "NOT in the inventory");
	CheckCreativeHud(Controller, "phone granted", true);
	CallOnRep(Controller, "ClientCreativePhoneCreated");

	// told again 1.5 s, 5 s, 8.5 s and 12 s later (the first call comes before the client has the item)
	PhoneKicks[Controller] = { GetTickCount64() + 1500, 4, 0 };
}

void CreativeIsland::SetupSettingsMachine(AFortVolume* Volume)
{
	if (!Volume)
		return;

	CreativeMinigame::NoteGameThread();

	static auto MachineClass = FindObject<UClass>(L"/Game/Athena/Items/Gameplay/MinigameSettingsControl/MinigameSettingsMachine.MinigameSettingsMachine_C");
	static auto MinigameVolumeComponentClass = FindObject<UClass>(L"/Script/FortniteGame.FortMinigameVolumeComponent");
	static auto CreativeLinkComponentClass = FindObject<UClass>(L"/Script/FortniteGame.FortCreativeVolumeLinkComponent");

	if (!MachineClass)
	{
		LOG_WARN(LogCreative, "MinigameSettingsMachine_C was not found, so the island has no settings and Start Game cannot work");
		return;
	}

	auto VolumeComponent = MinigameVolumeComponentClass ? (UObject*)Volume->GetComponentByClass(MinigameVolumeComponentClass) : nullptr;
	auto LinkComponent = CreativeLinkComponentClass ? (UObject*)Volume->GetComponentByClass(CreativeLinkComponentClass) : nullptr;

	LOG_INFO(LogCreative, "Island volume components: minigame volume {}, creative link {}", __int64(VolumeComponent), __int64(LinkComponent));

	static auto CurrentMachineOffset = VolumeComponent ? VolumeComponent->GetOffset("CurrentMinigameSettingsMachine", false) : -1;

	if (VolumeComponent && CurrentMachineOffset != -1 && VolumeComponent->Get<UObject*>(CurrentMachineOffset))
	{
		LOG_INFO(LogCreative, "The island already has a settings machine");
		return;
	}

	auto Machine = GetWorld()->SpawnActor<AActor>(MachineClass, Volume->GetActorLocation());

	if (!Machine)
	{
		LOG_ERROR(LogCreative, "Spawning the settings machine failed");
		return;
	}

	LOG_INFO(LogCreative, "Spawned the settings machine {}", Machine->GetName());

	static auto SettingsVolumeOffset = Machine->GetOffset("SettingsVolume", false);
	static auto MachineLinkOffset = Machine->GetOffset("CreativeLinkComponent", false);

	if (SettingsVolumeOffset != -1)
	{
		Machine->Get<AFortVolume*>(SettingsVolumeOffset) = Volume;
		CallOnRep(Machine, "OnRep_SettingsVolume");
	}

	if (MachineLinkOffset != -1 && LinkComponent)
		Machine->Get<UObject*>(MachineLinkOffset) = LinkComponent;

	if (VolumeComponent && CurrentMachineOffset != -1)
		VolumeComponent->Get<UObject*>(CurrentMachineOffset) = Machine;
}

// One logging pass-through per creative function the client can call. The client's call is not touched, it only gets written down,
// so after a run the log shows what the Select Type / Confirm buttons actually ask the server to do.
#define XENBONO_CREATIVE_LOG_HOOK(Name)                                                                                      \
	static void (*Name##Original)(UObject*, FFrame&, void*) = nullptr;                                                       \
	static void Name##Hook(UObject* Context, FFrame& Stack, void* Ret)                                                      \
	{                                                                                                                        \
		LOG_INFO(LogCreative, "Client called " #Name " on {}", Context ? Context->GetName() : std::string("null"));          \
		return Name##Original(Context, Stack, Ret);                                                                          \
	}

// The same, and it also writes the whole creative state, for the calls where the state at that moment matters.
#define XENBONO_CREATIVE_DUMP_HOOK(Name)                                                                                     \
	static void (*Name##Original)(UObject*, FFrame&, void*) = nullptr;                                                       \
	static void Name##Hook(UObject* Context, FFrame& Stack, void* Ret)                                                      \
	{                                                                                                                        \
		LOG_INFO(LogCreative, "Client called " #Name " on {}", Context ? Context->GetName() : std::string("null"));          \
		CreativeIsland::DumpState("before " #Name);                                                                          \
		Name##Original(Context, Stack, Ret);                                                                                 \
		CreativeIsland::DumpState("after " #Name);                                                                           \
	}

XENBONO_CREATIVE_LOG_HOOK(ServerClaimPortal)
XENBONO_CREATIVE_LOG_HOOK(ServerReleasePortal)
XENBONO_CREATIVE_DUMP_HOOK(ServerLoadPlotForPortal)
XENBONO_CREATIVE_LOG_HOOK(ServerLoadPlotForPortalFromMnemonic)
XENBONO_CREATIVE_LOG_HOOK(ServerLoadIslandCheckpoint)
XENBONO_CREATIVE_LOG_HOOK(ServerSaveIslandCheckpoint)
XENBONO_CREATIVE_LOG_HOOK(ServerReloadCreativePlot)
XENBONO_CREATIVE_LOG_HOOK(ServerEndLoadingVolume)
XENBONO_CREATIVE_LOG_HOOK(ServerEndUnloadingVolume)
XENBONO_CREATIVE_LOG_HOOK(ServerSetOnCreativePortalLoadedDelegate)
// The game calls this every time a prefab is picked in the menu (it sets CurrentPlayset for the prefab to place), so it is only logged.
XENBONO_CREATIVE_LOG_HOOK(ServerSetPlayset)
// Start Game / End Game and the island portal.
XENBONO_CREATIVE_DUMP_HOOK(ServerStartMinigame)
// End Game: also tell the minigame driver, which calls the game's EndGame if the game itself does nothing.
static void (*ServerEndMinigameOriginal)(UObject*, FFrame&, void*) = nullptr;

static void ServerEndMinigameHook(UObject* Context, FFrame& Stack, void* Ret)
{
	LOG_INFO(LogCreative, "Client called ServerEndMinigame on {}", Context ? Context->GetName() : std::string("null"));
	CreativeIsland::DumpState("before ServerEndMinigame");
	CreativeMinigame::NoteEndRequested(Context);
	ServerEndMinigameOriginal(Context, Stack, Ret);
	CreativeIsland::DumpState("after ServerEndMinigame");
}

XENBONO_CREATIVE_DUMP_HOOK(ServerRestartMinigame)
XENBONO_CREATIVE_LOG_HOOK(ServerSetMinigameClassSlot)
XENBONO_CREATIVE_LOG_HOOK(ServerToggleAutoRestartMinigame)
XENBONO_CREATIVE_LOG_HOOK(ServerRespondToAbandonMatch)
XENBONO_CREATIVE_LOG_HOOK(ServerStartInteractWithIslandPortal)
XENBONO_CREATIVE_LOG_HOOK(ServerTeleportViaCreativePortal)
XENBONO_CREATIVE_LOG_HOOK(ServerTryFindPlotItemForDefinition)
XENBONO_CREATIVE_LOG_HOOK(ServerStartLoadingVolume)
XENBONO_CREATIVE_LOG_HOOK(ServerStartUnloadingVolume)
XENBONO_CREATIVE_LOG_HOOK(ServerStopSavingCreativePlot)
XENBONO_CREATIVE_LOG_HOOK(ServerResetCurrentVolumeWithoutTeleport)
XENBONO_CREATIVE_LOG_HOOK(ServerSpawnCreativeSupplyDrop)

static void HookOne(UObject* ControllerDefault, const char* Name, void* Detour, void** Original)
{
	auto Function = FindObject<UFunction>(std::string("/Script/FortniteGame.FortPlayerControllerAthena.") + Name);

	if (!Function)
	{
		LOG_WARN(LogCreative, "Creative function {} was not found on this version", Name);
		return;
	}

	LOG_INFO(LogCreative, "Hooking creative function {}: {}", Name, Hooking::MinHook::Hook(ControllerDefault, Function, Detour, Original, false, true) ? "ok" : "FAILED");
}

void CreativeIsland::HookControllerFunctions()
{
	static auto ControllerDefault = FindObject(L"/Script/FortniteGame.Default__FortPlayerControllerAthena");

	if (!ControllerDefault)
	{
		LOG_ERROR(LogCreative, "Default__FortPlayerControllerAthena was not found, creative functions are not hooked");
		return;
	}

#define XENBONO_HOOK_CREATIVE(Name) HookOne(ControllerDefault, #Name, Name##Hook, (void**)&Name##Original)
	XENBONO_HOOK_CREATIVE(ServerClaimPortal);
	XENBONO_HOOK_CREATIVE(ServerReleasePortal);
	XENBONO_HOOK_CREATIVE(ServerLoadPlotForPortal);
	XENBONO_HOOK_CREATIVE(ServerLoadPlotForPortalFromMnemonic);
	XENBONO_HOOK_CREATIVE(ServerLoadIslandCheckpoint);
	XENBONO_HOOK_CREATIVE(ServerSaveIslandCheckpoint);
	XENBONO_HOOK_CREATIVE(ServerReloadCreativePlot);
	XENBONO_HOOK_CREATIVE(ServerEndLoadingVolume);
	XENBONO_HOOK_CREATIVE(ServerEndUnloadingVolume);
	XENBONO_HOOK_CREATIVE(ServerSetOnCreativePortalLoadedDelegate);
	XENBONO_HOOK_CREATIVE(ServerSetPlayset);
	XENBONO_HOOK_CREATIVE(ServerStartMinigame);
	XENBONO_HOOK_CREATIVE(ServerEndMinigame);
	XENBONO_HOOK_CREATIVE(ServerRestartMinigame);
	XENBONO_HOOK_CREATIVE(ServerSetMinigameClassSlot);
	XENBONO_HOOK_CREATIVE(ServerToggleAutoRestartMinigame);
	XENBONO_HOOK_CREATIVE(ServerRespondToAbandonMatch);
	XENBONO_HOOK_CREATIVE(ServerStartInteractWithIslandPortal);
	XENBONO_HOOK_CREATIVE(ServerTeleportViaCreativePortal);
	XENBONO_HOOK_CREATIVE(ServerTryFindPlotItemForDefinition);
	XENBONO_HOOK_CREATIVE(ServerStartLoadingVolume);
	XENBONO_HOOK_CREATIVE(ServerStartUnloadingVolume);
	XENBONO_HOOK_CREATIVE(ServerStopSavingCreativePlot);
	XENBONO_HOOK_CREATIVE(ServerResetCurrentVolumeWithoutTeleport);
	XENBONO_HOOK_CREATIVE(ServerSpawnCreativeSupplyDrop);
#undef XENBONO_HOOK_CREATIVE
}


// ---------------------------------------------------------------------------------------------------------------------
// DumpState
// ---------------------------------------------------------------------------------------------------------------------
static std::string Describe(UObject* Object)
{
	return Object ? Object->GetName() + " (" + std::to_string(__int64(Object)) + ")" : std::string("null");
}

static std::string Where(AActor* Actor)
{
	if (!Actor)
		return "null";

	auto Location = Actor->GetActorLocation();
	return std::format("{} {} {}", Location.X, Location.Y, Location.Z);
}

static int CountActors(const wchar_t* ClassPath)
{
	auto Class = FindObject<UClass>(ClassPath);

	if (!Class)
		return -1;

	auto Actors = UGameplayStatics::GetAllActorsOfClass(GetWorld(), Class);
	const int Count = Actors.Num();
	Actors.Free();
	return Count;
}

static void DumpVolume(const char* Label, AFortVolume* Volume)
{
	if (!Volume)
	{
		LOG_INFO(LogCreative, "  {}: null", Label);
		return;
	}

	LOG_INFO(LogCreative, "  {}: {} at {}", Label, Describe(Volume), Where(Volume));
	LOG_INFO(LogCreative, "    VolumeState {} (3 = ready)", (int)Volume->GetVolumeState());

	static auto CurrentPlaysetOffset = Volume->GetOffset("CurrentPlayset", false);

	if (CurrentPlaysetOffset != -1)
		LOG_INFO(LogCreative, "    CurrentPlayset {}", Describe(Volume->Get<UObject*>(CurrentPlaysetOffset)));

	static auto LevelSaveClass = FindObject<UClass>("/Script/FortniteGame.FortLevelSaveComponent");
	static auto StreamClass = FindObject<UClass>("/Script/FortniteGame.PlaysetLevelStreamComponent");
	static auto MinigameClass = FindObject<UClass>("/Script/FortniteGame.FortMinigameVolumeComponent");
	static auto LinkClass = FindObject<UClass>("/Script/FortniteGame.FortCreativeVolumeLinkComponent");

	auto LevelSave = LevelSaveClass ? (UObject*)Volume->GetComponentByClass(LevelSaveClass) : nullptr;
	auto Stream = StreamClass ? (UObject*)Volume->GetComponentByClass(StreamClass) : nullptr;
	auto MinigameComponent = MinigameClass ? (UObject*)Volume->GetComponentByClass(MinigameClass) : nullptr;
	auto Link = LinkClass ? (UObject*)Volume->GetComponentByClass(LinkClass) : nullptr;

	LOG_INFO(LogCreative, "    components: level save {}, playset stream {}, minigame {}, creative link {}", Describe(LevelSave), Describe(Stream), Describe(MinigameComponent), Describe(Link));
	CreativeIsland::LogBudget("volume dump", 1000);

	if (LevelSave)
	{
		auto Bool = [&](const char* Name) -> std::string {
			auto Offset = LevelSave->GetOffset(Name, false);
			return Offset == -1 ? "n/a" : (LevelSave->Get<bool>(Offset) ? "true" : "false");
		};

		static auto PlotOffset = LevelSave->GetOffset("RestrictedPlotDefinition", false);
		static auto PermissionsOffset = LevelSave->GetOffset("PlotPermissions", false);
		static auto LoadedPlotOffset = LevelSave->GetOffset("LoadedPlot", false);

		LOG_INFO(LogCreative, "    level save: bIsLoaded {}, bLoadPlaysetFromPlot {}, bAutoLoadFromRestrictedPlotDefinition {}, bCurated {}", Bool("bIsLoaded"), Bool("bLoadPlaysetFromPlot"), Bool("bAutoLoadFromRestrictedPlotDefinition"), Bool("bCurated"));
		LOG_INFO(LogCreative, "    level save: RestrictedPlotDefinition {}, LoadedPlot {}, PlotPermissions {} (0 private, 1 public)", Describe(PlotOffset == -1 ? nullptr : LevelSave->Get<UObject*>(PlotOffset)), Describe(LoadedPlotOffset == -1 ? nullptr : LevelSave->Get<UObject*>(LoadedPlotOffset)), PermissionsOffset == -1 ? -1 : (int)LevelSave->Get<uint8_t>(PermissionsOffset));
	}

	if (Stream)
	{
		static auto StreamPlaysetOffset = Stream->GetOffset("CurrentPlayset", false);
		LOG_INFO(LogCreative, "    stream component CurrentPlayset {}", Describe(StreamPlaysetOffset == -1 ? nullptr : Stream->Get<UObject*>(StreamPlaysetOffset)));
	}

	if (MinigameComponent)
	{
		static auto MachineOffset = MinigameComponent->GetOffset("CurrentMinigameSettingsMachine", false);
		LOG_INFO(LogCreative, "    settings machine {}", Describe(MachineOffset == -1 ? nullptr : MinigameComponent->Get<UObject*>(MachineOffset)));
	}
}

static void LogIslandStreaming(const std::string& Reason);

void CreativeIsland::DumpState(const std::string& Reason, int MaxTimes)
{
	static std::map<std::string, int> Times;

	if (Times[Reason]++ >= MaxTimes)
		return;

	LOG_INFO(LogCreative, "======== creative state: {} ========", Reason);

	LOG_INFO(LogCreative, "actors: {} volumes, {} minigames, {} portals, {} building pieces", CountActors(L"/Script/FortniteGame.FortVolume"), CountActors(L"/Script/FortniteGame.FortMinigame"), CountActors(L"/Script/FortniteGame.FortAthenaCreativePortal"), CountActors(L"/Script/FortniteGame.BuildingSMActor"));

	static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");

	if (!ControllerClass)
	{
		LOG_WARN(LogCreative, "FortPlayerControllerAthena was not found, cannot list players");
		return;
	}

	auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

	for (int i = 0; i < Controllers.Num(); ++i)
	{
		auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);

		if (!Controller)
			continue;

		LOG_INFO(LogCreative, "player {}", Describe(Controller));

		auto Pawn = Controller->GetMyFortPawn();
		LOG_INFO(LogCreative, "  pawn {} at {}", Describe(Pawn), Where(Pawn));

		static auto OwnedPortalOffset = Controller->GetOffset("OwnedPortal", false);
		static auto CurrentPlaysetOffset = Controller->GetOffset("CurrentPlayset", false);

		auto OwnedPortal = OwnedPortalOffset == -1 ? nullptr : Controller->Get<AFortAthenaCreativePortal*>(OwnedPortalOffset);

		LOG_INFO(LogCreative, "  OwnedPortal {}, CurrentPlayset {}", Describe(OwnedPortal), Describe(CurrentPlaysetOffset == -1 ? nullptr : Controller->Get<UObject*>(CurrentPlaysetOffset)));

		auto LinkedVolume = Controller->GetCreativePlotLinkedVolume();
		DumpVolume("CreativePlotLinkedVolume", LinkedVolume);

		// The minigame that belongs to this island: its state decides what Start Game can do.
		static auto VolumeManagerDefault = FindObject<UObject>(L"/Script/FortniteGame.Default__FortVolumeManager");

		if (VolumeManagerDefault && LinkedVolume)
		{
			if (auto GetMinigameFn = VolumeManagerDefault->FindFunction("GetMinigameForVolume"))
			{
				struct { AFortVolume* Volume; UObject* ReturnValue; } Params{ LinkedVolume, nullptr };
				VolumeManagerDefault->ProcessEvent(GetMinigameFn, &Params);
				auto Minigame = Params.ReturnValue;

				static const char* StateNames[] = { "PreGame", "Setup", "Transitioning", "WaitingForCameras", "Warmup", "InProgress", "PostGameTimeDilation", "PostRoundEnd", "PostGameEnd", "PostGameAbandon", "PostGameReset" };

				if (Minigame)
				{
					static auto CurrentStateOffset = Minigame->GetOffset("CurrentState", false);
					static auto PreviousStateOffset = Minigame->GetOffset("PreviousState", false);
					static auto AutoStartDelayOffset = Minigame->GetOffset("AutoStartDelay", false);

					const int Current = CurrentStateOffset == -1 ? -1 : (int)Minigame->Get<uint8_t>(CurrentStateOffset);
					const int Previous = PreviousStateOffset == -1 ? -1 : (int)Minigame->Get<uint8_t>(PreviousStateOffset);

					LOG_INFO(LogCreative, "  island minigame {}: state {} ({}), previous {}, AutoStartDelay {}", Describe(Minigame), Current, Current >= 0 && Current <= 10 ? StateNames[Current] : "?", Previous, AutoStartDelayOffset == -1 ? -1 : Minigame->Get<int>(AutoStartDelayOffset));
				}
				else
					LOG_WARN(LogCreative, "  GetMinigameForVolume found no minigame for the island volume");
			}
		}

		// The game's own answers about this player and island.
		if (auto OwnsFn = Controller->FindFunction("OwnsIslandVolume"))
		{
			struct { AFortVolume* Volume; bool ReturnValue; } Params{ LinkedVolume, false };
			Controller->ProcessEvent(OwnsFn, &Params);
			LOG_INFO(LogCreative, "  OwnsIslandVolume(linked volume): {}", Params.ReturnValue);
		}

		if (auto PlayerState = Controller->GetPlayerState())
		{
			if (auto CanEditFn = PlayerState->FindFunction("CanEditCreativeIsland"))
			{
				struct { bool ReturnValue; } Params{ false };
				PlayerState->ProcessEvent(CanEditFn, &Params);
				LOG_INFO(LogCreative, "  CanEditCreativeIsland: {}", Params.ReturnValue);
			}
		}

		if (OwnedPortal)
		{
			auto Bool = [&](const char* Name) -> std::string {
				auto Offset = OwnedPortal->GetOffset(Name, false);
				return Offset == -1 ? "n/a" : (OwnedPortal->Get<bool>(Offset) ? "true" : "false");
			};

			LOG_INFO(LogCreative, "  portal {} at {}: bPortalOpen {}, bUserInitiatedLoad {}, bInErrorState {}, bIsPublishedPortal {}, bReturnToCreativeHub {}", Describe(OwnedPortal), Where(OwnedPortal), Bool("bPortalOpen"), Bool("bUserInitiatedLoad"), Bool("bInErrorState"), Bool("bIsPublishedPortal"), Bool("bReturnToCreativeHub"));
			DumpVolume("portal LinkedVolume", OwnedPortal->GetLinkedVolume());
		}
	}

	Controllers.Free();
	LOG_INFO(LogCreative, "======== end of creative state ========");

	if (Reason.find("island set up") != std::string::npos)
		LogIslandStreaming(Reason);
}

// ---------------------------------------------------------------------------------------------------------------------
// Probes and raw memory helpers, for working out how a prefab is stored (the layouts of some record structs are not in the SDK)
// ---------------------------------------------------------------------------------------------------------------------
static bool Readable(const void* Pointer, size_t Size)
{
	if (!Pointer)
		return false;

	MEMORY_BASIC_INFORMATION Info{};

	if (!VirtualQuery(Pointer, &Info, sizeof(Info)) || Info.State != MEM_COMMIT || (Info.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
		return false;

	return (const char*)Pointer + Size <= (const char*)Info.BaseAddress + Info.RegionSize;
}

static void LogHex(const std::string& Label, const void* Pointer, size_t Size)
{
	if (!Readable(Pointer, Size))
	{
		LOG_WARN(LogCreative, "{}: {} bytes at {} are not readable", Label, Size, __int64(Pointer));
		return;
	}

	for (size_t Row = 0; Row < Size; Row += 16)
	{
		std::string Line;

		for (size_t i = Row; i < Row + 16 && i < Size; ++i)
			Line += std::format("{:02x} ", ((const unsigned char*)Pointer)[i]);

		LOG_INFO(LogCreative, "{} +0x{:02x}: {}", Label, Row, Line);
	}
}

static std::string FloatsAt(const void* Pointer, int Count)
{
	if (!Readable(Pointer, Count * sizeof(float)))
		return "unreadable";

	std::string Text;

	for (int i = 0; i < Count; ++i)
		Text += std::format("{:.1f} ", ((const float*)Pointer)[i]);

	return Text;
}

// A name read out of raw memory: only trusted when the index looks like a real one.
static std::string NameAt(const void* Pointer)
{
	if (!Readable(Pointer, 8))
		return "unreadable";

	const int Index = *(const int*)Pointer;

	if (Index <= 0 || Index > 60000000)
		return std::format("(not a name: {})", Index);

	return ((const FName*)Pointer)->ToString();
}

struct ActorProbe
{
	std::string Label;
	std::set<AActor*> Before;
	ULONGLONG Started = 0;
	FVector Anchor;
	std::string AnchorName;
	int Stage = 0;
};

static std::vector<ActorProbe> Probes;

void CreativeIsland::StartActorProbe(const std::string& Label, AActor* Anchor)
{
	static auto ActorClass = FindObject<UClass>(L"/Script/Engine.Actor");

	if (!ActorClass || Probes.size() > 8)
		return;

	ActorProbe Probe;
	Probe.Label = Label;
	Probe.Started = GetTickCount64();

	if (Anchor)
		Probe.Anchor = Anchor->GetActorLocation();

	Probe.AnchorName = Anchor ? Anchor->GetName() : std::string("no anchor");

	auto Actors = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ActorClass);

	for (int i = 0; i < Actors.Num(); ++i)
		Probe.Before.insert(Actors.at(i));

	Actors.Free();
	LOG_INFO(LogCreative, "Actor probe '{}' started: {} actors exist, anchor {} at {}", Label, (int)Probe.Before.size(), Probe.AnchorName, Where(Anchor));
	Probes.push_back(std::move(Probe));
}

// ---------------------------------------------------------------------------------------------------------------------
// Pieces players build: do they replicate? (A piece the client never gets is invisible there, and the client then builds on the ground next to it.)
// ---------------------------------------------------------------------------------------------------------------------
struct PieceWatch
{
	AActor* Actor;
	std::string Name;
	ULONGLONG Placed;
};

static std::vector<PieceWatch> PieceWatches;
static int PieceWatchesLogged = 0;

void CreativeIsland::WatchPiece(AActor* Piece)
{
	if (!Piece)
		return;

	// make sure the piece replicates (the same nudge the prefab pieces get)
	{
		static auto ReplicatesOffset = Piece->GetOffset("bReplicates", false);
		static auto ReplicatesMask = ReplicatesOffset == -1 ? (uint8_t)0 : GetFieldMask(Piece->GetProperty("bReplicates"));
		static auto SetReplicatesFn = FindObject<UFunction>(L"/Script/Engine.Actor.SetReplicates");

		const bool bReplicates = ReplicatesOffset != -1 && ReplicatesMask && Piece->ReadBitfieldValue(ReplicatesOffset, ReplicatesMask);

		if (!bReplicates && SetReplicatesFn)
		{
			bool bNew = true;
			Piece->ProcessEvent(SetReplicatesFn, &bNew);
			LOG_WARN(LogCreative, "Piece watch: {} did not replicate, it does now", Piece->GetName());
		}

		Piece->FlushNetDormancy();
		Piece->ForceNetUpdate();
	}

	if (PieceWatchesLogged + (int)PieceWatches.size() >= 40)
		return;

	PieceWatches.push_back({ Piece, Piece->GetName(), GetTickCount64() });
}

static bool PieceAlive(void* Object)
{
	if (!Object || !Readable(Object, 0x30))
		return false;

	if (ChunkedObjects)
		return ChunkedObjects->IsValid((UObject*)Object);

	if (UnchunkedObjects)
		return UnchunkedObjects->IsValid((UObject*)Object);

	return true;
}

static void CheckPieceWatches()
{
	if (PieceWatches.empty())
		return;

	const ULONGLONG Now = GetTickCount64();

	for (size_t i = 0; i < PieceWatches.size();)
	{
		auto Watch = PieceWatches[i];

		if (Now - Watch.Placed < 1500)
		{
			++i;
			continue;
		}

		PieceWatches.erase(PieceWatches.begin() + i);
		++PieceWatchesLogged;

		if (!PieceAlive(Watch.Actor))
		{
			LOG_INFO(LogCreative, "Piece watch: {} no longer exists", Watch.Name);
			continue;
		}

		auto Actor = Watch.Actor;
		int LevelInitialized = -1;
		const int Channels = ActorChannelCount(Actor, &LevelInitialized);

		auto Bit = [&](const char* Property) -> int {
			const int Offset = Actor->GetOffset(Property, false);
			return Offset == -1 ? -1 : (int)Actor->ReadBitfieldValue(Offset, GetFieldMask(Actor->GetProperty(Property)));
		};
		auto Byte = [&](const char* Property) -> int {
			const int Offset = Actor->GetOffset(Property, false);
			return Offset == -1 ? -1 : (int)*(unsigned char*)((char*)Actor + Offset);
		};

		LOG_INFO(LogCreative, "Piece watch: {} at {}: {} client connections have a channel for it, level initialised {}, bReplicates {}, RemoteRole {}, NetDormancy {}, hidden {}, always relevant {}", Watch.Name, Where(Actor), Channels,
			LevelInitialized, Bit("bReplicates"), Byte("RemoteRole"), Byte("NetDormancy"), Bit("bHidden"), Bit("bAlwaysRelevant"));
	}
}

void CreativeIsland::TickProbes()
{
	TickPhone();
	CheckPieceWatches();

	if (Probes.empty())
		return;

	static auto ActorClass = FindObject<UClass>(L"/Script/Engine.Actor");
	const ULONGLONG Now = GetTickCount64();
	static const ULONGLONG Stages[] = { 500, 2000, 6000 };

	for (size_t p = 0; p < Probes.size();)
	{
		auto& Probe = Probes[p];

		if (Probe.Stage >= 3)
		{
			Probes.erase(Probes.begin() + p);
			continue;
		}

		if (Now - Probe.Started < Stages[Probe.Stage])
		{
			++p;
			continue;
		}

		const int Stage = Probe.Stage++;
		(void)Stage;
		auto Actors = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ActorClass);
		int NewCount = 0;
		std::map<std::string, int> ByClass;
		std::vector<std::string> Samples;

		for (int i = 0; i < Actors.Num(); ++i)
		{
			auto Actor = Actors.at(i);

			if (!Actor || Probe.Before.count(Actor))
				continue;

			++NewCount;
			++ByClass[Actor->ClassPrivate ? Actor->ClassPrivate->GetName() : std::string("?")];

			if (Samples.size() < 6)
			{
				const auto Location = Actor->GetActorLocation();
				Samples.push_back(std::format("{} at {} ({:+.0f} {:+.0f} {:+.0f} from the anchor)", Actor->GetName(), Where(Actor), (double)Location.X - (double)Probe.Anchor.X, (double)Location.Y - (double)Probe.Anchor.Y, (double)Location.Z - (double)Probe.Anchor.Z));
			}
		}

		Actors.Free();

		std::string Classes;

		for (auto& Entry : ByClass)
			Classes += std::format(" {}x{}", Entry.second, Entry.first);

		LOG_INFO(LogCreative, "Actor probe '{}' after {} ms: {} new actors:{}", Probe.Label, (int)(Now - Probe.Started), NewCount, Classes.empty() ? std::string(" none") : Classes);

		for (auto& Sample : Samples)
			LOG_INFO(LogCreative, "  new actor {}", Sample);

		++p;
	}
}

// How many entries a TMap holds, from its raw memory: TSparseArray { TArray Data (+0), TBitArray AllocationFlags (+0x10), FirstFreeIndex (+0x30), NumFreeIndices (+0x34) }.
static int MapCount(const char* Map)
{
	if (!Readable(Map, 0x38))
		return -1;

	return *(const int*)(Map + 8) - *(const int*)(Map + 0x34);
}

// Index of the first allocated entry of a TMap (bit i of the allocation flags), or -1.
static int FirstMapEntry(const char* Map)
{
	if (!Readable(Map, 0x38))
		return -1;

	const int Slots = *(const int*)(Map + 8);
	const unsigned* Flags = (const unsigned*)(Map + 0x10); // inline storage of the first 128 bits

	for (int i = 0; i < Slots && i < 128; ++i)
		if (Flags[i / 32] & (1u << (i % 32)))
			return i;

	return -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Generic property dump: what an object (or struct) really holds, read through the game's own reflection data, so nothing in it is a guess about
// a layout. Every property is written with its type, offset and value. A struct that has no reflected properties (the saved-record structs are
// written and read by native code) is written as raw bytes.
// ---------------------------------------------------------------------------------------------------------------------
namespace PropDump
{
	// FField and FProperty in UE 4.25 (the Offset_Internal of 0x4c that the startup log prints belongs to this layout)
	constexpr int FieldClassAt = 0x08;
	constexpr int ArrayDimAt = 0x38;
	constexpr int ElementSizeAt = 0x3C;
	constexpr int InnerAt = 0x78;  // StructProperty::Struct, ObjectProperty::PropertyClass, ArrayProperty::Inner, MapProperty::KeyProp, ByteProperty::Enum, EnumProperty::UnderlyingProp
	constexpr int SecondAt = 0x80; // MapProperty::ValueProp, EnumProperty::Enum
	constexpr int MapLayoutAt = 0x88;

	struct Context
	{
		std::string Label;
		int Lines = 0;
		int MaxLines = 220;
	};

	static void Emit(Context& Ctx, int Indent, const std::string& Text)
	{
		if (Ctx.Lines > Ctx.MaxLines)
			return;

		if (Ctx.Lines == Ctx.MaxLines)
		{
			++Ctx.Lines;
			LOG_INFO(LogCreative, "{}: ... (the rest is cut)", Ctx.Label);
			return;
		}

		++Ctx.Lines;
		LOG_INFO(LogCreative, "{}: {}{}", Ctx.Label, std::string(Indent * 2, ' '), Text);
	}

	static bool IsLiveObject(const void* Object)
	{
		if (!Object || !Readable(Object, 0x30))
			return false;

		if (ChunkedObjects)
			return ChunkedObjects->IsValid((UObject*)Object);

		if (UnchunkedObjects)
			return UnchunkedObjects->IsValid((UObject*)Object);

		return true;
	}

	static std::string ObjectText(const void* Pointer)
	{
		if (!Pointer)
			return "null";

		if (!IsLiveObject(Pointer))
			return std::format("(not a live object: {})", Pointer);

		return ((UObject*)Pointer)->GetFullName();
	}

	static std::string TypeOf(const void* Property)
	{
		auto FieldClass = *(const void* const*)((const char*)Property + FieldClassAt);
		return Readable(FieldClass, 8) ? NameAt(FieldClass) : std::string("?");
	}

	static std::string BytesText(const unsigned char* Bytes, size_t Count)
	{
		std::string Text;

		for (size_t i = 0; i < Count; ++i)
			Text += std::format("{:02x} ", Bytes[i]);

		return Text;
	}

	// The name of an enum value, from the enum's own table of names (the layout the rest of this project already relies on).
	static std::string EnumName(const void* Enum, long long Value)
	{
		if (!IsLiveObject(Enum))
			return "";

		auto Names = (TArray<TPair<FName, int64>>*)(__int64(Enum) + sizeof(UField) + sizeof(FString));

		if (!Readable(Names, sizeof(*Names)) || Names->Num() < 0 || Names->Num() > 4096 || !Readable(Names->GetData(), (size_t)Names->Num() * sizeof(TPair<FName, int64>)))
			return "";

		for (int i = 0; i < Names->Num(); ++i)
		{
			auto& Pair = Names->At(i);

			if (Pair.Value() == Value)
			{
				auto Text = NameAt(&Pair.Key());
				const auto Colons = Text.rfind("::");
				return Colons == std::string::npos ? Text : Text.substr(Colons + 2);
			}
		}

		return "";
	}

	static void DumpStruct(Context& Ctx, int Indent, int Depth, const void* Struct, const char* Base, int Size);
	static void DumpValue(Context& Ctx, int Indent, int Depth, const std::string& Name, const void* Property, const char* Address);

	static void DumpProperty(Context& Ctx, int Indent, int Depth, void* Property, const char* Base)
	{
		const std::string Name = NameAt((const char*)Property + Offsets::PropName);
		const int Offset = *(const int*)((const char*)Property + Offsets::Offset_Internal);
		const int Dim = *(const int*)((const char*)Property + ArrayDimAt);
		const int Size = *(const int*)((const char*)Property + ElementSizeAt);

		for (int i = 0; i < Dim && i < 4; ++i)
			DumpValue(Ctx, Indent, Depth, std::format("{}{} @0x{:x}", Name, Dim > 1 ? std::format("[{}]", i) : std::string(), Offset + i * Size), Property, Base + Offset + (size_t)i * Size);
	}

	static void DumpValue(Context& Ctx, int Indent, int Depth, const std::string& Name, const void* Property, const char* Address)
	{
		const char* Prop = (const char*)Property;
		const std::string Type = TypeOf(Property);
		const int Size = *(const int*)(Prop + ElementSizeAt);

		auto Line = [&](const std::string& Value) { Emit(Ctx, Indent, std::format("{} <{}>: {}", Name, Type, Value)); };

		if (Size <= 0 || Size > 0x100000 || !Readable(Address, (size_t)Size))
		{
			Line("(unreadable)");
			return;
		}

		if (Type == "BoolProperty")
		{
			const unsigned char ByteOffset = *(const unsigned char*)(Prop + 0x79);
			const unsigned char FieldMask = *(const unsigned char*)(Prop + 0x7B);
			Line((*(const unsigned char*)(Address + ByteOffset) & FieldMask) ? "true" : "false");
		}
		else if (Type == "ByteProperty")
		{
			const int Value = *(const unsigned char*)Address;
			const auto EnumText = EnumName(*(const void* const*)(Prop + InnerAt), Value);
			Line(EnumText.empty() ? std::to_string(Value) : std::format("{} ({})", Value, EnumText));
		}
		else if (Type == "EnumProperty")
		{
			const auto Underlying = *(const char* const*)(Prop + InnerAt);
			const int UnderlyingSize = Underlying && Readable(Underlying, 0x40) ? *(const int*)(Underlying + ElementSizeAt) : 1;
			const long long Value = UnderlyingSize == 8 ? *(const long long*)Address : UnderlyingSize == 4 ? *(const int*)Address : UnderlyingSize == 2 ? *(const short*)Address : *(const unsigned char*)Address;
			const auto EnumText = EnumName(*(const void* const*)(Prop + SecondAt), Value);
			Line(EnumText.empty() ? std::to_string(Value) : std::format("{} ({})", Value, EnumText));
		}
		else if (Type == "Int8Property")
			Line(std::to_string((int)*(const signed char*)Address));
		else if (Type == "Int16Property")
			Line(std::to_string(*(const short*)Address));
		else if (Type == "UInt16Property")
			Line(std::to_string(*(const unsigned short*)Address));
		else if (Type == "IntProperty")
			Line(std::to_string(*(const int*)Address));
		else if (Type == "UInt32Property")
			Line(std::to_string(*(const unsigned*)Address));
		else if (Type == "Int64Property")
			Line(std::to_string(*(const long long*)Address));
		else if (Type == "UInt64Property")
			Line(std::to_string(*(const unsigned long long*)Address));
		else if (Type == "FloatProperty")
			Line(std::format("{}", *(const float*)Address));
		else if (Type == "DoubleProperty")
			Line(std::format("{}", *(const double*)Address));
		else if (Type == "NameProperty")
			Line(NameAt(Address));
		else if (Type == "StrProperty")
		{
			const wchar_t* Chars = *(const wchar_t* const*)Address;
			const int Count = *(const int*)(Address + 8);
			std::string Text;

			if (Chars && Count > 1 && Count < 4096 && Readable(Chars, (size_t)Count * 2))
				for (int i = 0; i < Count - 1 && i < 120; ++i)
					Text += (Chars[i] >= 32 && Chars[i] < 127) ? (char)Chars[i] : '?';

			Line("\"" + Text + "\"");
		}
		else if (Type == "TextProperty")
			Line("(text)");
		else if (Type == "SoftObjectProperty" || Type == "SoftClassProperty")
		{
			auto& Soft = *(TSoftObjectPtr<UObject>*)Address;
			Line(Soft.IsValid() ? Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("not set"));
		}
		else if (Type == "ObjectProperty" || Type == "ClassProperty" || Type == "InterfaceProperty")
			Line(ObjectText(*(const void* const*)Address));
		else if (Type == "WeakObjectProperty" || Type == "LazyObjectProperty")
			Line("weak/lazy reference: " + BytesText((const unsigned char*)Address, (size_t)(std::min)(Size, 16)));
		else if (Type.find("DelegateProperty") != std::string::npos)
			Line("(delegate)");
		else if (Type == "StructProperty")
		{
			const void* Struct = *(const void* const*)(Prop + InnerAt);
			const std::string StructName = IsLiveObject(Struct) ? ((UObject*)Struct)->GetName() : std::string("?");

			if (StructName == "Vector" || StructName == "Rotator")
				Line(FloatsAt(Address, 3));
			else if (StructName == "Quat")
				Line(FloatsAt(Address, 4));
			else if (StructName == "Transform")
				Line("rotation " + FloatsAt(Address, 4) + "location " + FloatsAt(Address + 0x10, 3) + "scale " + FloatsAt(Address + 0x20, 3));
			else if (StructName == "Guid")
				Line(BytesText((const unsigned char*)Address, 16));
			else
			{
				Line("struct " + StructName + ", " + std::to_string(Size) + " bytes" + (Depth <= 0 ? " {...}" : ""));

				if (Depth > 0)
					DumpStruct(Ctx, Indent + 1, Depth - 1, Struct, Address, Size);
			}
		}
		else if (Type == "ArrayProperty")
		{
			const char* Inner = *(const char* const*)(Prop + InnerAt);
			const char* Data = *(const char* const*)Address;
			const int Num = *(const int*)(Address + 8);

			if (!Inner || !Readable(Inner, 0x80) || Num < 0 || Num > 10000000)
			{
				Line("(array with an unexpected layout)");
				return;
			}

			const int InnerSize = *(const int*)(Inner + ElementSizeAt);
			const std::string InnerType = TypeOf(Inner);
			Line(std::format("{} elements of {}", Num, InnerType));

			const int Bytes = (std::min)(Num, 64);

			if (Num > 0 && (InnerType == "ByteProperty" || InnerType == "Int8Property") && Readable(Data, (size_t)Bytes))
			{
				Emit(Ctx, Indent + 1, "bytes: " + BytesText((const unsigned char*)Data, (size_t)Bytes));
				return;
			}

			if (Depth <= 0 || InnerSize <= 0 || !Data)
				return;

			for (int i = 0; i < Num && i < 6; ++i)
				DumpValue(Ctx, Indent + 1, Depth - 1, std::format("[{}]", i), Inner, Data + (size_t)i * InnerSize);
		}
		else if (Type == "MapProperty")
		{
			const char* KeyProp = *(const char* const*)(Prop + InnerAt);
			const char* ValueProp = *(const char* const*)(Prop + SecondAt);
			const int KeyOffset = 0;
			const int ValueOffset = *(const int*)(Prop + MapLayoutAt);
			const int Stride = *(const int*)(Prop + MapLayoutAt + 12); // the size of one entry with its hash links
			const int Count = MapCount(Address);

			Line(std::format("{} entries, key {}, value {} (key at +{}, value at +{}, {} bytes per entry; layout ints {} {} {} {} {} {})", Count, KeyProp && Readable(KeyProp, 0x80) ? TypeOf(KeyProp) : std::string("?"),
				ValueProp && Readable(ValueProp, 0x80) ? TypeOf(ValueProp) : std::string("?"), KeyOffset, ValueOffset, Stride, *(const int*)(Prop + MapLayoutAt), *(const int*)(Prop + MapLayoutAt + 4),
				*(const int*)(Prop + MapLayoutAt + 8), *(const int*)(Prop + MapLayoutAt + 12), *(const int*)(Prop + MapLayoutAt + 16), *(const int*)(Prop + MapLayoutAt + 20)));

			if (Count <= 0 || Depth <= 0 || !KeyProp || !ValueProp || !Readable(KeyProp, 0x80) || !Readable(ValueProp, 0x80) || Stride < 8 || Stride > 0x400)
				return;

			const char* Data = *(const char* const*)Address;
			const int Slots = *(const int*)(Address + 8);
			const int NumBits = *(const int*)(Address + 0x28);
			const unsigned* Flags = NumBits > 128 ? *(const unsigned* const*)(Address + 0x20) : (const unsigned*)(Address + 0x10);

			if (!Data || Slots <= 0 || Slots > 10000000 || !Readable(Flags, (size_t)((Slots + 31) / 32) * 4))
				return;

			int Shown = 0;

			for (int i = 0; i < Slots && Shown < 6; ++i)
			{
				if (!(Flags[i / 32] & (1u << (i % 32))))
					continue;

				const char* Entry = Data + (size_t)i * Stride;

				if (!Readable(Entry, (size_t)Stride))
					break;

				DumpValue(Ctx, Indent + 1, Depth - 1, std::format("key[{}]", Shown), KeyProp, Entry + KeyOffset);
				DumpValue(Ctx, Indent + 1, Depth - 1, std::format("value[{}]", Shown), ValueProp, Entry + ValueOffset);
				++Shown;
			}
		}
		else if (Type == "SetProperty")
			Line(std::format("{} elements", MapCount(Address)));
		else
			Line("(" + Type + ") " + BytesText((const unsigned char*)Address, (size_t)(std::min)(Size, 24)));
	}

	static void DumpStruct(Context& Ctx, int Indent, int Depth, const void* Struct, const char* Base, int Size)
	{
		if (!IsLiveObject(Struct))
		{
			Emit(Ctx, Indent, "(no struct information)");
			return;
		}

		bool bAny = false;

		for (auto S = (const char*)Struct; S && IsLiveObject(S); S = (const char*)((UStruct*)S)->GetSuperStruct())
		{
			for (void* Prop = *(void**)(S + Offsets::Children); Prop && Readable(Prop, 0x80); Prop = GetNext(Prop))
			{
				bAny = true;
				DumpProperty(Ctx, Indent, Depth, Prop, Base);
			}
		}

		// Nothing reflected: the layout is only known to the native code, so show the bytes.
		if (!bAny && Size > 0 && Readable(Base, (size_t)Size))
		{
			const int Shown = (std::min)(Size, 0x90);

			for (int Row = 0; Row < Shown; Row += 16)
				Emit(Ctx, Indent, std::format("raw +0x{:02x}: {}", Row, BytesText((const unsigned char*)Base + Row, (size_t)(std::min)(16, Shown - Row))));

			if (Shown < Size)
				Emit(Ctx, Indent, std::format("raw ... {} more bytes", Size - Shown));
		}
	}

	static void DumpObject(const std::string& Label, UObject* Object, int Depth, int MaxLines)
	{
		if (!IsLiveObject(Object))
		{
			LOG_INFO(LogCreative, "{}: no object", Label);
			return;
		}

		Context Ctx;
		Ctx.Label = Label;
		Ctx.MaxLines = MaxLines;
		Emit(Ctx, 0, std::format("{} at {}", Object->GetFullName(), (const void*)Object));
		DumpStruct(Ctx, 1, Depth, Object->ClassPrivate, (const char*)Object, 0);
	}
}

// What a saved record (the thing a prefab's props are stored as) contains, as far as the SDK shows it, plus raw bytes of its first entries so the
// layout of the opaque actor records can be worked out from the log.
static void DescribeRecord(const std::string& Label, UObject* Record)
{
	if (!Record)
	{
		LOG_INFO(LogCreative, "{}: no record", Label);
		return;
	}

	static auto PackageOffset = Record->GetOffset("PackageName", false);
	static auto VersionOffset = Record->GetOffset("SaveVersion", false);
	static auto CenterOffset = Record->GetOffset("Center", false);
	static auto HalfBoundsOffset = Record->GetOffset("HalfBoundsExtent", false);
	static auto RotationOffset = Record->GetOffset("Rotation", false);
	static auto ScaleOffset = Record->GetOffset("Scale", false);
	static auto TemplatesOffset = Record->GetOffset("TemplateRecords", false);
	static auto InstancesOffset = Record->GetOffset("ActorInstanceRecords", false);
	static auto VolumeInfoOffset = Record->GetOffset("VolumeInfoActorRecords", false);
	static auto GridOffset = Record->GetOffset("bRequiresGridPlacement", false);
	const char* Base = (const char*)Record;

	LOG_INFO(LogCreative, "{}: {} package {}, version {}, center {}, half bounds {}, rotation {}, scale {}, needs grid {}", Label, Describe(Record), PackageOffset == -1 ? "n/a" : NameAt(Base + PackageOffset),
		VersionOffset == -1 ? -1 : (int)*(const short*)(Base + VersionOffset), CenterOffset == -1 ? "n/a" : FloatsAt(Base + CenterOffset, 3), HalfBoundsOffset == -1 ? "n/a" : FloatsAt(Base + HalfBoundsOffset, 3),
		RotationOffset == -1 ? "n/a" : FloatsAt(Base + RotationOffset, 3), ScaleOffset == -1 ? "n/a" : FloatsAt(Base + ScaleOffset, 3), GridOffset == -1 ? -1 : (int)*(const bool*)(Base + GridOffset));

	LOG_INFO(LogCreative, "{}: {} templates, {} actor instances, {} volume info actors", Label, TemplatesOffset == -1 ? -1 : MapCount(Base + TemplatesOffset), InstancesOffset == -1 ? -1 : MapCount(Base + InstancesOffset),
		VolumeInfoOffset == -1 ? -1 : MapCount(Base + VolumeInfoOffset));

	// The first template and the first instance, as raw bytes (element sizes worked out from the SDK: instance 0x90, template 0x68).
	if (TemplatesOffset != -1)
	{
		const char* Map = Base + TemplatesOffset;
		const int First = FirstMapEntry(Map);

		if (First >= 0 && Readable(Map, 8))
			LogHex(Label + " template " + std::to_string(First), *(const char* const*)Map + (size_t)First * 0x68, 0x68);
	}

	if (InstancesOffset != -1)
	{
		const char* Map = Base + InstancesOffset;
		const int First = FirstMapEntry(Map);

		if (First >= 0 && Readable(Map, 8))
			LogHex(Label + " instance " + std::to_string(First), *(const char* const*)Map + (size_t)First * 0x90, 0x90);
	}
}

// The saved records of a playset's props: from the record collection (soft record, name, transform: 0x60 bytes each), or from the props themselves.
static void PlaysetRecords(UFortPlaysetItemDefinition* Playset, std::vector<UObject*>& OutRecords, bool bLog)
{
	static auto CollectionOffset = Playset->GetOffset("PlaysetPropLevelSaveRecordCollection", false);
	static auto PropsOffset = Playset->GetOffset("AssociatedPlaysetProps", false);
	static auto UsePropsOffset = Playset->GetOffset("bUsePlaysetProps", false);
	static auto RecordClass = FindObject<UClass>(L"/Script/FortniteGame.LevelSaveRecord");
	static auto PropClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetPropItemDefinition");

	UObject* Collection = CollectionOffset == -1 ? nullptr : Playset->Get<UObject*>(CollectionOffset);
	char* Items = nullptr;
	int ItemCount = 0;

	if (Collection)
	{
		static auto ItemsOffset = Collection->GetOffset("Items", false);

		if (ItemsOffset != -1 && Readable((char*)Collection + ItemsOffset, 16))
		{
			Items = *(char**)((char*)Collection + ItemsOffset);
			ItemCount = *(int*)((char*)Collection + ItemsOffset + 8);
		}
	}

	char* Props = nullptr;
	int PropCount = 0;

	if (PropsOffset != -1 && Readable((char*)Playset + PropsOffset, 16))
	{
		Props = *(char**)((char*)Playset + PropsOffset);
		PropCount = *(int*)((char*)Playset + PropsOffset + 8);
	}

	if (bLog)
		LOG_INFO(LogCreative, "Prefab data: record collection {} with {} items, {} associated props, bUsePlaysetProps {}", Describe(Collection), ItemCount, PropCount, UsePropsOffset == -1 ? -1 : (int)((*(const unsigned char*)((char*)Playset + UsePropsOffset) >> 1) & 1));

	// a) the collection's items
	for (int i = 0; Items && i < ItemCount && i < 80; ++i)
	{
		char* Item = Items + (size_t)i * 0x60;

		if (!Readable(Item, 0x60))
			break;

		auto& Soft = *(TSoftObjectPtr<UObject>*)Item;
		const std::string Path = Soft.IsValid() ? Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("not set");

		if (bLog && i < 5)
			LOG_INFO(LogCreative, "Prefab data: collection item {}: record {}, name {}, rotation {}, location {}, scale {}", i, Path, NameAt(Item + 0x28), FloatsAt(Item + 0x30, 4), FloatsAt(Item + 0x40, 3), FloatsAt(Item + 0x50, 3));

		UObject* Record = Soft.IsValid() ? Soft.Get(RecordClass, true) : nullptr;

		if (Record)
			OutRecords.push_back(Record);
		else if (bLog && i < 5)
			LOG_WARN(LogCreative, "Prefab data: collection item {} could not be loaded", i);
	}

	// b) no collection: the props' own records
	for (int i = 0; OutRecords.empty() && Props && i < PropCount && i < 80; ++i)
	{
		char* Entry = Props + (size_t)i * 0x28;

		if (!Readable(Entry, 0x28))
			break;

		auto& Soft = *(TSoftObjectPtr<UObject>*)Entry;
		UObject* Prop = Soft.IsValid() ? Soft.Get(PropClass, true) : nullptr;

		if (bLog && i < 5)
			LOG_INFO(LogCreative, "Prefab data: prop {}: {} -> {}", i, Soft.IsValid() ? Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("not set"), Describe(Prop));

		if (!Prop)
			continue;

		static auto RecordOffset = Prop->GetOffset("ActorSaveRecord", false);

		if (RecordOffset != -1)
			if (auto Record = Prop->Get<UObject*>(RecordOffset))
				OutRecords.push_back(Record);
	}
}

static UObject* FirstVolumeManager()
{
	static auto ManagerClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolumeManager");

	if (!ManagerClass)
		return nullptr;

	auto Managers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ManagerClass);
	UObject* Found = Managers.Num() > 0 ? Managers.at(0) : nullptr;
	Managers.Free();
	return Found;
}

// Is the volume manager waiting for something that would keep it from activating a new volume? (Volumes are activated from its queue, and not
// while islands are "loading": a counter the game's server code raises and lowers.)
static void CheckVolumeManager(AFortVolume* Volume)
{
	auto Manager = FirstVolumeManager();

	if (!Manager)
	{
		LOG_WARN(LogCreative, "Volume manager: none found");
		return;
	}

	auto AreLoading = [&]() -> bool {
		struct { bool ReturnValue; } Params{ false };

		if (auto Fn = Manager->FindFunction("AreIslandsLoading"))
			Manager->ProcessEvent(Fn, &Params);

		return Params.ReturnValue;
	};

	static auto StartupOffset = Manager->GetOffset("bInSpawningStartup", false);
	static auto ObjectsOffset = Manager->GetOffset("VolumeObjects", false);

	int Registered = -1;
	bool bContains = false;

	if (ObjectsOffset != -1)
	{
		auto& Objects = Manager->Get<TArray<AFortVolume*>>(ObjectsOffset);
		Registered = Objects.Num();

		for (int i = 0; i < Objects.Num(); ++i)
			bContains = bContains || Objects.at(i) == Volume;
	}

	LOG_INFO(LogCreative, "Volume manager {}: AreIslandsLoading {}, bInSpawningStartup {}, {} volumes registered, the prefab volume is {}registered", Describe(Manager), AreLoading(),
		StartupOffset == -1 ? -1 : (int)(*(const unsigned char*)((char*)Manager + StartupOffset) & 1), Registered, bContains ? "" : "NOT ");

	// If the manager thinks islands are still loading, the counter is lowered (it is raised by the game when an island starts loading and lowered when it
	// has finished; that second half may never run here).
	for (int i = 0; i < 4 && AreLoading(); ++i)
	{
		if (auto Fn = Manager->FindFunction("DecrementUserIslandLoading"))
		{
			Manager->ProcessEvent(Fn);
			LOG_INFO(LogCreative, "Volume manager: DecrementUserIslandLoading called, AreIslandsLoading is now {}", AreLoading());
		}
		else
			break;
	}

	// A volume that never registered is never in its queue.
	if (!bContains && Volume)
	{
		if (auto Fn = Volume->FindFunction("AddSelfToVolumeManager"))
		{
			Volume->ProcessEvent(Fn);
			LOG_INFO(LogCreative, "Volume manager: AddSelfToVolumeManager called on {}", Volume->GetName());
		}
	}
}

// Test (own tick box): the volume manager's own ApplyLevelSaveToWorld for the first few records of the prefab, with actor probes around each.
static void ApplyPrefabRecords(UFortPlaysetItemDefinition* Playset, AFortVolume* Volume)
{
	std::vector<UObject*> Records;
	PlaysetRecords(Playset, Records, false);
	LOG_INFO(LogCreative, "Prefab apply test: {} records found for {}", (int)Records.size(), Playset->GetName());

	auto Manager = FirstVolumeManager();
	auto ApplyFn = Manager ? Manager->FindFunction("ApplyLevelSaveToWorld") : nullptr;

	if (!ApplyFn)
	{
		LOG_WARN(LogCreative, "Prefab apply test: the volume manager has no ApplyLevelSaveToWorld");
		return;
	}

	for (size_t i = 0; i < Records.size() && i < 3; ++i)
	{
		DescribeRecord("Prefab apply test record " + std::to_string(i), Records[i]);
		CreativeIsland::StartActorProbe("ApplyLevelSaveToWorld record " + std::to_string(i), Volume);

		struct { UObject* SaveToSpawnFrom; bool bComplexShouldSpawn; } Params{ Records[i], true };
		LOG_INFO(LogCreative, "Prefab apply test: calling ApplyLevelSaveToWorld for record {}", (int)i);
		Manager->ProcessEvent(ApplyFn, &Params);
		LOG_INFO(LogCreative, "Prefab apply test: ApplyLevelSaveToWorld for record {} returned", (int)i);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// What this build really contains of the creative content (the client is not the dedicated server: its files may lack the server-only data)
// ---------------------------------------------------------------------------------------------------------------------
void CreativeIsland::LogPakFiles()
{
	char ExePath[MAX_PATH]{};
	GetModuleFileNameA(nullptr, ExePath, MAX_PATH);
	std::string Dir = ExePath;

	for (int i = 0; i < 3; ++i) // ...\FortniteGame\Binaries\Win64\<exe> -> ...\FortniteGame
	{
		const auto Slash = Dir.find_last_of("\\/");

		if (Slash == std::string::npos)
			break;

		Dir.resize(Slash);
	}

	const std::string Folder = Dir + "\\Content\\Paks";
	WIN32_FIND_DATAA Find{};
	HANDLE Handle = FindFirstFileA((Folder + "\\*").c_str(), &Find);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		LOG_WARN(LogCreative, "Pak files: {} could not be listed", Folder);
		return;
	}

	int Count = 0;
	unsigned long long Total = 0;
	std::vector<std::string> Unusual;
	std::string Row;
	int InRow = 0;

	do
	{
		if (Find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;

		const std::string Name = Find.cFileName;
		const unsigned long long Size = ((unsigned long long)Find.nFileSizeHigh << 32) | Find.nFileSizeLow;
		++Count;
		Total += Size;

		if (Name.find("-WindowsClient.pak") == std::string::npos || Name.find("Server") != std::string::npos || Name.find("reative") != std::string::npos || Name.find("laysets") != std::string::npos)
			Unusual.push_back(Name + " (" + std::to_string(Size >> 20) + " MB)");

		Row += Name + " " + std::to_string(Size >> 20) + " MB, ";

		if (++InRow == 5)
		{
			LOG_INFO(LogCreative, "Pak files: {}", Row);
			Row.clear();
			InRow = 0;
		}
	} while (FindNextFileA(Handle, &Find));

	FindClose(Handle);

	if (!Row.empty())
		LOG_INFO(LogCreative, "Pak files: {}", Row);

	LOG_INFO(LogCreative, "Pak files: {} files in {}, {} GB in all", Count, Folder, Total >> 30);

	for (auto& Name : Unusual)
		LOG_INFO(LogCreative, "Pak files: not an ordinary client chunk: {}", Name);

	if (Unusual.empty())
		LOG_INFO(LogCreative, "Pak files: every file is an ordinary client chunk (no server or creative chunk)");
}

static std::string SoftPathOf(UObject* Object, const char* Property)
{
	const int Offset = Object->GetOffset(Property, false);

	if (Offset == -1)
		return "n/a";

	auto& Soft = Object->Get<TSoftObjectPtr<UObject>>(Offset);
	return Soft.IsValid() ? Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("-");
}

static int ArrayCountOf(UObject* Object, const char* Property)
{
	const int Offset = Object->GetOffset(Property, false);
	return Offset == -1 || !Readable((char*)Object + Offset, 16) ? -1 : *(int*)((char*)Object + Offset + 8);
}

// ---------------------------------------------------------------------------------------------------------------------
// The cooked asset registry: lists the assets of the build whether they are loaded or not, so it shows which content exists for the playsets
// ---------------------------------------------------------------------------------------------------------------------
static bool SafeProcessEvent(UObject* Object, UFunction* Function, void* Params)
{
	__try
	{
		Object->ProcessEvent(Function, Params);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static bool ContainsNoCase(const std::string& Text, const char* Word)
{
	std::string A = Text, B = Word;
	std::transform(A.begin(), A.end(), A.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	std::transform(B.begin(), B.end(), B.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return A.find(B) != std::string::npos;
}

static std::string WideText(const wchar_t* Chars, int Count)
{
	std::string Text;

	if (Chars && Count > 1 && Count < 4096 && Readable(Chars, (size_t)Count * 2))
		for (int i = 0; i < Count - 1; ++i)
			Text += (Chars[i] >= 32 && Chars[i] < 127) ? (char)Chars[i] : '?';

	return Text;
}

static void SurveyAssetRegistry()
{
	auto Helpers = FindObject<UObject>(L"/Script/AssetRegistry.Default__AssetRegistryHelpers");
	auto GetRegistryFn = Helpers ? Helpers->FindFunction("GetAssetRegistry") : nullptr;

	if (!GetRegistryFn)
	{
		LOG_WARN(LogCreative, "Asset registry: AssetRegistryHelpers.GetAssetRegistry was not found");
		return;
	}

	struct { UObject* Object; void* Interface; } Registry{};

	if (!SafeProcessEvent(Helpers, GetRegistryFn, &Registry) || !PropDump::IsLiveObject(Registry.Object))
	{
		LOG_WARN(LogCreative, "Asset registry: no registry object came back ({})", (const void*)Registry.Object);
		return;
	}

	LOG_INFO(LogCreative, "Asset registry: {}", Registry.Object->GetFullName());

	auto Lookup = [&](const char* Name, const wchar_t* InterfacePath) -> UFunction* {
		UFunction* Function = Registry.Object->FindFunction(Name);

		if (!Function)
			Function = FindObject<UFunction>(InterfacePath);

		LOG_INFO(LogCreative, "Asset registry: function {} {}", Name, Function ? "found" : "NOT FOUND");
		return Function;
	};

	auto ByPathFn = Lookup("GetAssetsByPath", L"/Script/AssetRegistry.AssetRegistry:GetAssetsByPath");
	auto ByClassFn = Lookup("GetAssetsByClass", L"/Script/AssetRegistry.AssetRegistry:GetAssetsByClass");
	auto PathsFn = Lookup("GetAllCachedPaths", L"/Script/AssetRegistry.AssetRegistry:GetAllCachedPaths");

	// The package paths that look like creative content.
	if (PathsFn)
	{
		struct { TArray<FString> OutPathList; } Params{};

		if (SafeProcessEvent(Registry.Object, PathsFn, &Params))
		{
			const int Total = Params.OutPathList.Num();
			int Shown = 0;

			LOG_INFO(LogCreative, "Asset registry: {} package paths are known", Total);

			for (int i = 0; i < Total && Shown < 160; ++i)
			{
				auto& Path = Params.OutPathList.at(i);
				const std::string Text = WideText(*(const wchar_t**)&Path, *(const int*)((const char*)&Path + 8));

				if (ContainsNoCase(Text, "playset") || ContainsNoCase(Text, "creative") || ContainsNoCase(Text, "prefab") || ContainsNoCase(Text, "gallery") || ContainsNoCase(Text, "devices"))
				{
					LOG_INFO(LogCreative, "Asset registry path: {}", Text);
					++Shown;
				}
			}
		}
		else
			LOG_WARN(LogCreative, "Asset registry: GetAllCachedPaths failed");
	}

	// What an asset list holds: a count per class, and the assets that matter for the playsets.
	auto Summarize = [&](const std::string& Label, TArray<char>& Assets, int MaxListed, bool bOnlyWorldsAndNames) {
		const int Num = Assets.Num();
		std::map<std::string, int> ByClass;
		int Shown = 0;

		for (int i = 0; i < Num; ++i)
		{
			const char* Asset = (const char*)Assets.GetData() + (size_t)i * 0x50;

			if (!Readable(Asset, 0x50))
				break;

			const std::string Class = NameAt(Asset + 0x20);
			const std::string Package = NameAt(Asset + 0x08);
			++ByClass[Class];

			const bool bInteresting = !bOnlyWorldsAndNames || Class == "World" || ContainsNoCase(Package, "barn") || ContainsNoCase(Package, "barrel") || ContainsNoCase(Package, "creatureplacer");

			if (bInteresting && Shown < MaxListed)
			{
				LOG_INFO(LogCreative, "Asset registry {}: {} [{}]", Label, Package, Class);
				++Shown;
			}
		}

		std::string Classes;

		for (auto& Entry : ByClass)
			Classes += std::format(" {}x{}", Entry.second, Entry.first);

		LOG_INFO(LogCreative, "Asset registry {}: {} assets, by class:{}", Label, Num, Classes.empty() ? std::string(" none") : Classes);
	};

	if (ByPathFn)
	{
		for (const wchar_t* Root : { L"/Game/Playsets", L"/Game/Creative" })
		{
			struct { FName PackagePath; TArray<char> OutAssetData; bool bRecursive; bool bIncludeOnlyOnDiskAssets; bool ReturnValue; } Params{};
			Params.PackagePath = UKismetStringLibrary::Conv_StringToName(Root);
			Params.bRecursive = true;
			Params.bIncludeOnlyOnDiskAssets = false;

			if (SafeProcessEvent(Registry.Object, ByPathFn, &Params))
				Summarize(std::string("under ") + std::string(Root, Root + wcslen(Root)), Params.OutAssetData, 80, true);
			else
				LOG_WARN(LogCreative, "Asset registry: GetAssetsByPath failed");
		}
	}

	if (ByClassFn)
	{
		for (const wchar_t* Class : { L"FortPlaysetItemDefinition", L"FortPlaysetPropItemDefinition", L"FortPlaysetGrenadeItemDefinition", L"LevelSaveRecord" })
		{
			struct { FName ClassName; TArray<char> OutAssetData; bool bSearchSubClasses; bool ReturnValue; } Params{};
			Params.ClassName = UKismetStringLibrary::Conv_StringToName(Class);
			Params.bSearchSubClasses = false;

			if (SafeProcessEvent(Registry.Object, ByClassFn, &Params))
				Summarize(std::string("class ") + std::string(Class, Class + wcslen(Class)), Params.OutAssetData, 40, false);
			else
				LOG_WARN(LogCreative, "Asset registry: GetAssetsByClass failed");
		}
	}
}

// Every playset, prop and record the game has loaded, summarised: which of them carry any data to spawn something from.
void CreativeIsland::DumpCreativeData(const char* Why)
{
	static int Times = 0;

	if (Times++ >= 6)
		return;

	LOG_INFO(LogCreative, "=== Creative data survey ({}) ===", Why);
	LogPakFiles();
	SurveyAssetRegistry();

	static auto PlaysetClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetItemDefinition");
	static auto PropClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetPropItemDefinition");
	static auto RecordClass = FindObject<UClass>(L"/Script/FortniteGame.LevelSaveRecord");

	// --- playsets
	{
		auto Playsets = GetAllObjectsOfClass<UObject>(PlaysetClass);
		int WithLevel = 0, WithItem = 0, WithRecord = 0, WithCollection = 0, WithProps = 0, Listed = 0;
		UObject* ExampleLevel = nullptr;
		UObject* ExampleItem = nullptr;
		UObject* ExampleRecord = nullptr;
		UObject* ExampleProps = nullptr;

		for (auto Playset : Playsets)
		{
			if (Playset->GetName().rfind("Default__", 0) == 0)
				continue;

			const auto Level = SoftPathOf(Playset, "PlaysetToSpawn");
			const auto Item = SoftPathOf(Playset, "SpawnItem");
			const int RecordOffset = Playset->GetOffset("LevelSaveRecord", false);
			const int CollectionOffset = Playset->GetOffset("PlaysetPropLevelSaveRecordCollection", false);
			const bool bRecord = RecordOffset != -1 && Playset->Get<UObject*>(RecordOffset);
			const bool bCollection = CollectionOffset != -1 && Playset->Get<UObject*>(CollectionOffset);
			const int Props = ArrayCountOf(Playset, "AssociatedPlaysetProps");

			WithLevel += Level != "-" && Level != "n/a";
			WithItem += Item != "-" && Item != "n/a";
			WithRecord += bRecord;
			WithCollection += bCollection;
			WithProps += Props > 0;

			if (!ExampleLevel && Level != "-" && Level != "n/a") ExampleLevel = Playset;
			if (!ExampleItem && Item != "-" && Item != "n/a") ExampleItem = Playset;
			if (!ExampleRecord && (bRecord || bCollection)) ExampleRecord = Playset;
			if (!ExampleProps && Props > 0) ExampleProps = Playset;

			if (Listed++ < 120)
				LOG_INFO(LogCreative, "Survey playset {}: level {}, spawn item {}, level save record {}, record collection {}, {} props", Playset->GetName(), Level, Item, bRecord ? "yes" : "no", bCollection ? "yes" : "no", Props);
		}

		LOG_INFO(LogCreative, "Survey playsets: {} loaded; {} with a level to stream, {} with a spawn item, {} with a level save record, {} with a record collection, {} with props", Listed, WithLevel, WithItem, WithRecord, WithCollection, WithProps);

		PropDump::DumpObject("Survey example playset with a level", ExampleLevel, 3, 160);
		PropDump::DumpObject("Survey example playset with a spawn item", ExampleItem, 3, 160);
		PropDump::DumpObject("Survey example playset with a record", ExampleRecord, 3, 160);
		PropDump::DumpObject("Survey example playset with props", ExampleProps, 3, 160);
	}

	// --- props
	{
		auto Props = GetAllObjectsOfClass<UObject>(PropClass);
		int WithRecord = 0, Listed = 0;
		UObject* Example = nullptr;

		for (auto Prop : Props)
		{
			if (Prop->GetName().rfind("Default__", 0) == 0)
				continue;

			const int Offset = Prop->GetOffset("ActorSaveRecord", false);
			const bool bRecord = Offset != -1 && Prop->Get<UObject*>(Offset);
			WithRecord += bRecord;
			++Listed;

			if (bRecord && !Example)
				Example = Prop;
		}

		LOG_INFO(LogCreative, "Survey props: {} loaded, {} with an actor save record", Listed, WithRecord);

		if (Example)
		{
			PropDump::DumpObject("Survey example prop with a record", Example, 3, 120);
			DescribeRecord("Survey example prop record", Example->Get<UObject*>(Example->GetOffset("ActorSaveRecord", false)));
		}
	}

	// --- records
	{
		auto Records = GetAllObjectsOfClass<UObject>(RecordClass);
		int Listed = 0;

		for (auto Record : Records)
		{
			if (Record->GetName().rfind("Default__", 0) == 0)
				continue;

			if (Listed++ < 30)
			{
				static auto TemplatesOffset = Record->GetOffset("TemplateRecords", false);
				static auto InstancesOffset = Record->GetOffset("ActorInstanceRecords", false);
				LOG_INFO(LogCreative, "Survey record {}: {} templates, {} actor instances", Record->GetFullName(), TemplatesOffset == -1 ? -1 : MapCount((char*)Record + TemplatesOffset), InstancesOffset == -1 ? -1 : MapCount((char*)Record + InstancesOffset));
			}
		}

		LOG_INFO(LogCreative, "Survey records: {} level save records are loaded", Listed);
	}

	// --- the islands' level save components (the machinery that applies a record to the world and writes one from it)
	{
		static auto SaveClass = FindObject<UClass>(L"/Script/FortniteGame.LevelSaveBaseComponent");
		static auto SpawnerClass = FindObject<UClass>(L"/Script/FortniteGame.LevelRecordSpawner");
		auto Components = GetAllObjectsOfClass<UObject>(SaveClass);
		int Listed = 0;

		for (auto Component : Components)
		{
			if (Component->GetName().rfind("Default__", 0) == 0)
				continue;

			const int RecordOffset = Component->GetOffset("LevelRecord", false);
			UObject* Record = RecordOffset == -1 ? nullptr : Component->Get<UObject*>(RecordOffset);
			LOG_INFO(LogCreative, "Survey level save component {}: record {}", Component->GetFullName(), Describe(Record));

			if (Record && Listed++ < 3)
			{
				DescribeRecord("Survey level save component record", Record);
				PropDump::DumpObject("Survey level save component record dump", Record, 2, 120);
			}
		}

		LOG_INFO(LogCreative, "Survey level save components: {}; record spawners alive: {}", (int)Components.size(), (int)GetAllObjectsOfClass<UObject>(SpawnerClass).size());
	}

	LOG_INFO(LogCreative, "=== end of the creative data survey ===");
}

// What the world streams when an island is set up: every streaming level with its state, and what the island volume's stream component holds. If an
// island is missing for a player, this says whether the level for it was ever created.
static void LogIslandStreaming(const std::string& Reason)
{
	auto World = GetWorld();
	static auto StreamingOffset = World->GetOffset("StreamingLevels", false);

	if (StreamingOffset != -1)
	{
		auto& Levels = World->Get<TArray<UObject*>>(StreamingOffset);
		LOG_INFO(LogCreative, "Streaming levels ({}): {}", Reason, Levels.Num());

		for (int i = 0; i < Levels.Num() && i < 40; ++i)
		{
			auto Level = Levels.at(i);

			if (!Level)
				continue;

			static auto WorldAssetOffset = Level->GetOffset("WorldAsset", false);
			static auto FlagsOffset = Level->GetOffset("bShouldBeLoaded", false);
			std::string Path = Level->GetName();

			if (WorldAssetOffset != -1)
			{
				auto& Asset = Level->Get<TSoftObjectPtr<UObject>>(WorldAssetOffset);

				if (Asset.IsValid())
					Path = Asset.SoftObjectPtr.ObjectID.AssetPathName.ToString();
			}

			auto Ask = [&](const char* Function) -> int {
				struct { bool ReturnValue; } Params{ false };

				if (auto Fn = Level->FindFunction(Function))
				{
					Level->ProcessEvent(Fn, &Params);
					return Params.ReturnValue ? 1 : 0;
				}

				return -1;
			};

			const int Flags = FlagsOffset == -1 ? -1 : (int)*(unsigned char*)((char*)Level + FlagsOffset);
			LOG_INFO(LogCreative, "  {} ({}): loaded {}, visible {}, shouldBeLoaded {}, shouldBeVisible {}", Path, Level->GetName(), Ask("IsLevelLoaded"), Ask("IsLevelVisible"), Flags == -1 ? -1 : (Flags >> 4) & 1,
				Flags == -1 ? -1 : (Flags >> 3) & 1);
		}
	}

	static auto StreamClass = FindObject<UClass>(L"/Script/FortniteGame.PlaysetLevelStreamComponent");
	int Dumped = 0;

	for (auto Component : GetAllObjectsOfClass<UObject>(StreamClass))
	{
		if (Component->GetName().rfind("Default__", 0) == 0 || Component->GetName().find("GEN_VARIABLE") != std::string::npos)
			continue;

		const int PlaysetOffset = Component->GetOffset("CurrentPlayset", false);

		if (PlaysetOffset == -1 || !Component->Get<UObject*>(PlaysetOffset))
			continue;

		PropDump::DumpObject("Island stream component " + Component->GetFullName(), Component, 2, 70);

		if (++Dumped >= 3)
			break;
	}

	// the volume manager must have finished starting up before a volume spawns what it holds
	if (auto Manager = FirstVolumeManager())
	{
		static auto StartupOffset = Manager->GetOffset("bInSpawningStartup", false);
		struct { bool ReturnValue; } Loading{ false };

		if (auto Fn = Manager->FindFunction("AreIslandsLoading"))
			Manager->ProcessEvent(Fn, &Loading);

		LOG_INFO(LogCreative, "Volume manager ({}): AreIslandsLoading {}, bInSpawningStartup {}", Reason, Loading.ReturnValue, StartupOffset == -1 ? -1 : (int)(*(const unsigned char*)((char*)Manager + StartupOffset) & 1));
	}
}

void CreativeIsland::DumpObjectToLog(const std::string& Label, UObject* Object, int Depth, int MaxLines)
{
	PropDump::DumpObject(Label, Object, Depth, MaxLines);
}

// A playset that was just thrown: everything it holds, its first props and their records (the first few different playsets only).
static void InspectPlayset(UFortPlaysetItemDefinition* Playset)
{
	static std::set<std::string> Done;
	const auto Name = Playset->GetName();

	if (Done.size() >= 8 || !Done.insert(Name).second)
		return;

	if (Done.size() == 1)
		CreativeIsland::DumpCreativeData("the first thrown prefab");

	PropDump::DumpObject("Playset dump " + Name, Playset, 3, 260);

	static auto PropClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetPropItemDefinition");
	const int PropsOffset = Playset->GetOffset("AssociatedPlaysetProps", false);

	if (PropsOffset == -1 || !PropClass || !Readable((char*)Playset + PropsOffset, 16))
		return;

	char* Props = *(char**)((char*)Playset + PropsOffset);
	const int Count = *(int*)((char*)Playset + PropsOffset + 8);

	for (int i = 0; Props && i < Count && i < 3; ++i)
	{
		if (!Readable(Props + (size_t)i * 0x28, 0x28))
			break;

		auto& Soft = *(TSoftObjectPtr<UObject>*)(Props + (size_t)i * 0x28);
		UObject* Prop = Soft.IsValid() ? Soft.Get(PropClass, true) : nullptr;
		const std::string Label = "Prop dump " + Name + " #" + std::to_string(i);

		PropDump::DumpObject(Label, Prop, 3, 160);

		const int RecordOffset = Prop ? Prop->GetOffset("ActorSaveRecord", false) : -1;
		UObject* Record = RecordOffset == -1 ? nullptr : Prop->Get<UObject*>(RecordOffset);

		if (Record)
		{
			PropDump::DumpObject(Label + " record", Record, 2, 120);
			DescribeRecord(Label + " record", Record);
		}
		else if (Prop)
			LOG_INFO(LogCreative, "{}: the prop has no actor save record (pointer null)", Label);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Our own prefabs. The client's files hold no data for the real ones (no level, no saved records, no positions), so a thrown prefab is built from the
// ordinary player building pieces instead: a house as big as the prefab's footprint, with a door gap, two walls high and a flat roof. The pieces are
// the same ones players build with, so they replicate, can be edited and count towards the island's memory.
// ---------------------------------------------------------------------------------------------------------------------
static UClass* FindPieceClass(const char* ShortName, const wchar_t* Path)
{
	static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");
	static std::map<std::string, UClass*> Cache;

	if (auto It = Cache.find(ShortName); It != Cache.end() && It->second)
		return It->second;

	UClass* Found = nullptr;

	// A class some player already built with is loaded: look for it by name.
	for (auto Object : GetAllObjectsOfClass<UObject>(ClassClass))
	{
		if (Object->GetName() == ShortName)
		{
			Found = (UClass*)Object;
			break;
		}
	}

	if (!Found)
		Found = FindObject<UClass>(Path);

	if (!Found)
		Found = LoadObject<UClass>(Path, ClassClass);

	Cache[ShortName] = Found;
	return Found;
}

static bool InheritsFrom(UClass* Class, UClass* Base);

static ABuildingSMActor* SpawnPiece(UClass* Class, double X, double Y, double Z, float Yaw, AFortPlayerControllerAthena* Builder, int Team, double ScaleX = 1.0, double ScaleY = 1.0, double ScaleZ = 1.0)
{
	FTransform Transform{};
	FRotator Rotation{};
	Rotation.Yaw = Yaw;
	Transform.Translation = FVector((float)X, (float)Y, (float)Z);
	Transform.Rotation = Rotation.Quaternion();
	Transform.Scale3D = { (float)ScaleX, (float)ScaleY, (float)ScaleZ };

	auto Piece = GetWorld()->SpawnActor<ABuildingSMActor>(Class, Transform);

	if (!Piece)
		return nullptr;

	// The set pieces (castle walls, farm roofs, bank pillars ...) are placed in levels, where they need not replicate; spawned here they must, or no
	// client ever sees them. Player pieces replicate already.
	{
		static auto ReplicatesOffset = Piece->GetOffset("bReplicates", false);
		static auto ReplicatesMask = ReplicatesOffset == -1 ? (uint8_t)0 : GetFieldMask(Piece->GetProperty("bReplicates"));
		static auto SetReplicatesFn = FindObject<UFunction>(L"/Script/Engine.Actor.SetReplicates");
		static int NotReplicating = 0, Spawned = 0;

		++Spawned;

		const bool bReplicates = ReplicatesOffset != -1 && ReplicatesMask && Piece->ReadBitfieldValue(ReplicatesOffset, ReplicatesMask);

		if (!bReplicates && SetReplicatesFn)
		{
			bool bNew = true;
			Piece->ProcessEvent(SetReplicatesFn, &bNew);

			if (++NotReplicating <= 3)
				LOG_INFO(LogCreative, "Own prefab: {} did not replicate, it does now ({} of {} spawned pieces so far)", Class->GetName(), NotReplicating, Spawned);
		}

		Piece->FlushNetDormancy();
		Piece->ForceNetUpdate();
	}

	static auto BuildingSMClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingSMActor");
	static auto BuildingClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingActor");

	// Only a building piece is told it was placed by a player; other actors (props) are left as they spawn.
	if (BuildingSMClass && InheritsFrom(Class, BuildingSMClass))
	{
		Piece->SetPlayerPlaced(true);

		// The game's structural check destroys a piece nothing supports: the log of the 6 Nov test shows five roof pieces of the barn collapsing within a second of
		// it being built (BlueprintOnStructurallyUnstable, then RemoveActorWhenDied), which is why prefabs came out with holes in them. The set pieces are
		// scenery, held up by nothing the game's grid can see, so they are marked as supported (ABuildingSMActor::bForciblyStructurallySupported).
		static auto ForcedOffset = Piece->GetOffset("bForciblyStructurallySupported", false);
		static auto ForcedMask = ForcedOffset == -1 ? (uint8_t)0 : GetFieldMask(Piece->GetProperty("bForciblyStructurallySupported"));
		static int Marked = 0;

		if (ForcedOffset != -1 && ForcedMask)
		{
			Piece->SetBitfieldValue(ForcedOffset, ForcedMask, true);

			if (++Marked == 1)
			{
				LOG_INFO(LogCreative, "Own prefab: pieces are marked as structurally supported (bForciblyStructurallySupported at 0x{:x}, now {})", ForcedOffset, (int)Piece->ReadBitfieldValue(ForcedOffset, ForcedMask));
			}
		}
		else if (Marked == 0)
		{
			LOG_WARN(LogCreative, "Own prefab: {} has no bForciblyStructurallySupported, its pieces can collapse", Class->GetName());
		}
	}

	if (BuildingClass && InheritsFrom(Class, BuildingClass))
	{
		if (Builder)
			Piece->InitializeBuildingActor(Builder, Piece, true);

		if (Team >= 0)
			Piece->SetTeam((unsigned char)Team);
	}

	return Piece;
}

// ---------------------------------------------------------------------------------------------------------------------
// The playset's own list of the pieces it is made of (ActorClassCount: class and how many), built out of the real pieces (the castle's walls, the
// farm's roofs, the bank's balconies ...) instead of player pieces. The list says what and how many, not where, so the placement is ours: floors
// on the ground, walls round the edge (door in front, windows and arches spread out), a roof, stairs inside and the rest as decoration.
// ---------------------------------------------------------------------------------------------------------------------
struct ClassCount
{
	std::string Path;
	int Count = 0;
};

static std::vector<ClassCount> ReadClassCounts(UObject* Playset)
{
	std::vector<ClassCount> Result;
	auto Property = (const char*)Playset->GetProperty("ActorClassCount", false);

	if (!Property)
		return Result;

	const char* Map = (const char*)Playset + *(const int*)(Property + Offsets::Offset_Internal);
	const int ValueOffset = *(const int*)(Property + PropDump::MapLayoutAt);
	const int Stride = *(const int*)(Property + PropDump::MapLayoutAt + 12);
	const int Count = MapCount(Map);

	if (Count <= 0 || Stride < 8 || Stride > 0x400 || !Readable(Map, 0x38))
		return Result;

	const char* Data = *(const char* const*)Map;
	const int Slots = *(const int*)(Map + 8);
	const int NumBits = *(const int*)(Map + 0x28);
	const unsigned* Flags = NumBits > 128 ? *(const unsigned* const*)(Map + 0x20) : (const unsigned*)(Map + 0x10);

	if (!Data || Slots <= 0 || Slots > 100000 || !Readable(Flags, (size_t)((Slots + 31) / 32) * 4))
		return Result;

	for (int i = 0; i < Slots; ++i)
	{
		if (!(Flags[i / 32] & (1u << (i % 32))))
			continue;

		const char* Entry = Data + (size_t)i * Stride;

		if (!Readable(Entry, (size_t)Stride))
			break;

		auto& Soft = *(TSoftObjectPtr<UObject>*)Entry;
		const int Number = *(const int*)(Entry + ValueOffset);

		if (Soft.IsValid() && Number > 0 && Number < 2000)
			Result.push_back({ Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString(), Number });
	}

	return Result;
}

// ---------------------------------------------------------------------------------------------------------------------
// What a piece measures and how its top is shaped. Where each piece goes is worked out in PrefabArchitect.h, which knows nothing of the game: it is
// given the pieces as types with a box and a shape, and gives back positions. Each class is measured once (the box from the actor's bounds, the shape
// from a grid of rays dropped on it) and remembered.
// ---------------------------------------------------------------------------------------------------------------------
struct PieceInfo
{
	UClass* Class = nullptr;
	std::string Name;
	int Count = 0;
	Architect::Kind Kind = Architect::Kind::Prop;
	double OffsetX = 0, OffsetY = 0, OffsetZ = 0; // the middle of the box minus the actor's location (at yaw 0)
	double ExtentX = 75, ExtentY = 75, ExtentZ = 75; // half sizes at yaw 0
	bool bMeasured = false;
	Architect::Shape Shape = Architect::Shape::Box;
	int HighAxis = 0, HighSign = 0, RidgeAxis = 0;
	bool bShaped = false; // the rays gave the shape (else it is a guess from the name)
};

static bool ActorBoundsOf(AActor* Actor, double& OriginX, double& OriginY, double& OriginZ, double& ExtentX, double& ExtentY, double& ExtentZ)
{
	static auto Fn = FindObject<UFunction>(L"/Script/Engine.Actor.GetActorBounds");

	if (!Fn)
		return false;

	struct { bool bOnlyCollidingComponents; char Pad0[3]; float Origin[3]; float BoxExtent[3]; bool bIncludeFromChildActors; char Pad1[3]; } Params{};
	Actor->ProcessEvent(Fn, &Params);

	OriginX = Params.Origin[0]; OriginY = Params.Origin[1]; OriginZ = Params.Origin[2];
	ExtentX = Params.BoxExtent[0]; ExtentY = Params.BoxExtent[1]; ExtentZ = Params.BoxExtent[2];
	return ExtentX > 1 || ExtentY > 1 || ExtentZ > 1;
}

// One ray straight down against what is in the world (the measured piece, high above everything else). Gives the height it hit.
static bool TraceDown(double X, double Y, double FromZ, double ToZ, bool bComplex, double& HitZ)
{
	static auto Fn = FindObject<UFunction>(L"/Script/Engine.KismetSystemLibrary.LineTraceSingle");
	static auto Library = FindObject<UObject>(L"/Script/Engine.Default__KismetSystemLibrary");

	if (!Fn || !Library)
		return false;

	static const char* Path = "/Script/Engine.KismetSystemLibrary.LineTraceSingle";
	static const int Size = Fn->GetPropertiesSize();
	static const int WorldOffset = FindOffsetStruct(Path, "WorldContextObject");
	static const int StartOffset = FindOffsetStruct(Path, "Start");
	static const int EndOffset = FindOffsetStruct(Path, "End");
	static const int ChannelOffset = FindOffsetStruct(Path, "TraceChannel");
	static const int ComplexOffset = FindOffsetStruct(Path, "bTraceComplex");
	static const int IgnoreSelfOffset = FindOffsetStruct(Path, "bIgnoreSelf");
	static const int HitOffset = FindOffsetStruct(Path, "OutHit");
	static const int ReturnOffset = FindOffsetStruct(Path, "ReturnValue");
	static const int LocationOffset = FindOffsetStruct("/Script/Engine.HitResult", "Location");

	if (Size <= 0 || Size > 4096 || WorldOffset < 0 || StartOffset < 0 || EndOffset < 0 || ChannelOffset < 0 || ComplexOffset < 0 || IgnoreSelfOffset < 0 || HitOffset < 0 || ReturnOffset < 0 || LocationOffset < 0)
		return false;

	std::vector<uint8_t> Buffer((size_t)Size + 32, 0);
	auto At = [&](int Offset) { return Buffer.data() + Offset; };

	*(UObject**)At(WorldOffset) = GetWorld();
	*(FVector*)At(StartOffset) = FVector((float)X, (float)Y, (float)FromZ);
	*(FVector*)At(EndOffset) = FVector((float)X, (float)Y, (float)ToZ);
	*At(ChannelOffset) = 0; // Visibility
	*At(ComplexOffset) = bComplex ? 1 : 0;
	*At(IgnoreSelfOffset) = 0;

	Library->ProcessEvent(Fn, Buffer.data());

	if (!*At(ReturnOffset))
		return false;

	HitZ = (double)((FVector*)At(HitOffset + LocationOffset))->Z;
	return true;
}

// The shape of the top of a piece that was just spawned: rays are dropped on a grid over its box. A flat top is a box; one side high is a shed (a
// single slope, stairs, the end cap of a shed roof); high in the middle is a tent (a gable roof tile, the end cap of a gable).
static bool ProfileShape(PieceInfo& Info, double OX, double OY, double OZ)
{
	constexpr int N = 7;
	const double Bottom = OZ - Info.ExtentZ, Height = 2 * Info.ExtentZ;
	double H[N][N] = {};
	int Hits = 0;

	for (int Pass = 0; Pass < 2 && Hits == 0; ++Pass) // the detailed collision first, the simple one when that gives nothing
	{
		for (int iy = 0; iy < N; ++iy)
			for (int ix = 0; ix < N; ++ix)
			{
				const double X = OX + ((ix + 0.5) / N * 2.0 - 1.0) * Info.ExtentX * 0.97;
				const double Y = OY + ((iy + 0.5) / N * 2.0 - 1.0) * Info.ExtentY * 0.97;
				double Z = 0;

				if (TraceDown(X, Y, OZ + Info.ExtentZ + 120.0, Bottom - 60.0, Pass == 0, Z))
				{
					H[ix][iy] = (std::max)(0.0, (std::min)(Z - Bottom, Height));
					++Hits;
				}
			}
	}

	if (Hits < N * N / 4)
		return false;

	double ColX[N] = {}, ColY[N] = {};

	for (int ix = 0; ix < N; ++ix)
		for (int iy = 0; iy < N; ++iy)
		{
			ColX[ix] += H[ix][iy] / N;
			ColY[iy] += H[ix][iy] / N;
		}

	const double Threshold = 0.2 * Height;

	// 0 flat, 1 one side high (Sign: towards the larger coordinate or not), 2 high in the middle
	auto Analyse = [&](const double* P, int& Kind, int& Sign, double& Variation) {
		double Lo = P[0], Hi = P[0];

		for (int i = 1; i < N; ++i)
		{
			Lo = (std::min)(Lo, P[i]);
			Hi = (std::max)(Hi, P[i]);
		}

		Variation = Hi - Lo;
		const double E0 = (P[0] + P[1]) / 2, E1 = (P[N - 1] + P[N - 2]) / 2, Mid = (P[N / 2 - 1] + P[N / 2] + P[N / 2 + 1]) / 3;
		Kind = 0;
		Sign = 0;

		if (Mid - (std::max)(E0, E1) > 0.5 * Threshold)
			Kind = 2;
		else if (std::fabs(E1 - E0) >= Threshold)
		{
			Kind = 1;
			Sign = E1 > E0 ? 1 : -1;
		}
	};

	int KindX, SignX, KindY, SignY;
	double VariationX, VariationY;
	Analyse(ColX, KindX, SignX, VariationX);
	Analyse(ColY, KindY, SignY, VariationY);

	Info.Shape = Architect::Shape::Box;
	Info.bShaped = true;

	if (KindX == 0 && KindY == 0)
		return true;

	const bool bUseX = KindX != 0 && (KindY == 0 || VariationX >= VariationY);
	const int Axis = bUseX ? 0 : 1;
	const int Kind = bUseX ? KindX : KindY;
	const int Sign = bUseX ? SignX : SignY;

	if (Kind == 1)
	{
		Info.Shape = Architect::Shape::Shed;
		Info.HighAxis = Axis;
		Info.HighSign = Sign;
	}
	else
	{
		Info.Shape = Architect::Shape::Tent;
		Info.RidgeAxis = 1 - Axis;
	}

	return true;
}

static std::map<UClass*, PieceInfo> MeasuredPieces;

// The box and the shape of a piece: spawned high above the prefab, measured, removed again. Once per class.
static void MeasurePiece(PieceInfo& Info, const FVector& Probe)
{
	if (auto It = MeasuredPieces.find(Info.Class); It != MeasuredPieces.end())
	{
		const auto& Known = It->second;
		Info.OffsetX = Known.OffsetX; Info.OffsetY = Known.OffsetY; Info.OffsetZ = Known.OffsetZ;
		Info.ExtentX = Known.ExtentX; Info.ExtentY = Known.ExtentY; Info.ExtentZ = Known.ExtentZ;
		Info.bMeasured = Known.bMeasured;
		Info.Shape = Known.Shape; Info.HighAxis = Known.HighAxis; Info.HighSign = Known.HighSign; Info.RidgeAxis = Known.RidgeAxis; Info.bShaped = Known.bShaped;
		return;
	}

	// when nothing can be measured, a piece is about as big as its kind usually is
	switch (Info.Kind)
	{
	case Architect::Kind::Wall: case Architect::Kind::Window: case Architect::Kind::Opening: case Architect::Kind::Door: Info.ExtentX = 256; Info.ExtentY = 32; Info.ExtentZ = 192; break;
	case Architect::Kind::Floor: Info.ExtentX = 256; Info.ExtentY = 256; Info.ExtentZ = 16; break;
	case Architect::Kind::Roof: Info.ExtentX = 256; Info.ExtentY = 324; Info.ExtentZ = 220; break;
	case Architect::Kind::RoofCap: Info.ExtentX = 319; Info.ExtentY = 75; Info.ExtentZ = 220; break;
	case Architect::Kind::Stairs: Info.ExtentX = 128; Info.ExtentY = 256; Info.ExtentZ = 192; break;
	case Architect::Kind::Pillar: Info.ExtentX = 40; Info.ExtentY = 40; Info.ExtentZ = 192; break;
	case Architect::Kind::Rail: Info.ExtentX = 256; Info.ExtentY = 16; Info.ExtentZ = 56; break;
	case Architect::Kind::Feature: Info.ExtentX = 100; Info.ExtentY = 100; Info.ExtentZ = 200; break;
	default: Info.ExtentX = 75; Info.ExtentY = 75; Info.ExtentZ = 75; break;
	}

	FTransform Transform{};
	Transform.Translation = Probe;
	Transform.Scale3D = { 1, 1, 1 };

	auto Actor = GetWorld()->SpawnActor<AActor>(Info.Class, Transform);

	if (Actor)
	{
		double OX, OY, OZ, EX, EY, EZ;

		if (ActorBoundsOf(Actor, OX, OY, OZ, EX, EY, EZ))
		{
			Info.OffsetX = OX - (double)Probe.X;
			Info.OffsetY = OY - (double)Probe.Y;
			Info.OffsetZ = OZ - (double)Probe.Z;
			Info.ExtentX = EX; Info.ExtentY = EY; Info.ExtentZ = EZ;
			Info.bMeasured = true;

			// the top matters for roofs, their caps and stairs
			if (Info.Kind == Architect::Kind::Roof || Info.Kind == Architect::Kind::RoofCap || Info.Kind == Architect::Kind::Stairs)
				ProfileShape(Info, OX, OY, OZ);
		}

		static auto DestroyFn = FindObject<UFunction>(L"/Script/Engine.Actor.K2_DestroyActor");

		if (DestroyFn)
			Actor->ProcessEvent(DestroyFn);
	}

	// no ray result: the shape is guessed from the name and the box
	if (!Info.bShaped)
	{
		if (Info.Kind == Architect::Kind::Roof)
		{
			Info.Shape = Architect::Shape::Tent;
			Info.RidgeAxis = Info.ExtentX <= Info.ExtentY ? 0 : 1; // the ridge runs along the shorter side
		}
		else if (Info.Kind == Architect::Kind::RoofCap)
			Info.Shape = Architect::Shape::Tent;
		else if (Info.Kind == Architect::Kind::Stairs)
		{
			Info.Shape = Architect::Shape::Shed;
			Info.HighAxis = Info.ExtentX >= Info.ExtentY ? 0 : 1;
			Info.HighSign = 1;
		}
	}

	MeasuredPieces[Info.Class] = Info;
}

// How the placement went, for the log line of the prefab.
static int PlacedMoved = 0, PlacedTurned = 0, PlacedChecked = 0;
static double PlacedMaxShift = 0;

// Moves (and turns) a spawned actor, teleporting: nothing is swept or pushed.
static void SetActorPlacement(AActor* Actor, double X, double Y, double Z, float Yaw)
{
	static auto Fn = FindObject<UFunction>(L"/Script/Engine.Actor.K2_SetActorLocationAndRotation");

	if (!Fn)
		return;

	struct { float Location[3]; float Rotation[3]; bool bSweep; char Pad0[3]; char Hit[0x8C]; bool bTeleport; bool ReturnValue; char Pad1[2]; } Params{};
	Params.Location[0] = (float)X; Params.Location[1] = (float)Y; Params.Location[2] = (float)Z;
	Params.Rotation[1] = Yaw;
	Params.bTeleport = true;
	Actor->ProcessEvent(Fn, &Params);
}

// Places a piece so that the middle of its box is at (CX, CY) horizontally and its underside is at BottomZ. The position is worked out from the
// measured box, then checked against the box the spawned actor really has and corrected: nothing depends on the pivot or the turn being understood.
// AlongX: 1 when the piece belongs on an edge that runs along X, 0 along Y, -1 when its direction does not matter. A scale stretches the piece along
// its own axes (a roof is stretched to fit its span).
static ABuildingSMActor* PlaceBox(const PieceInfo& Info, double CX, double CY, double BottomZ, float Yaw, int AlongX, AFortPlayerControllerAthena* Builder, int Team, double Sx = 1, double Sy = 1, double Sz = 1)
{
	const double Radians = (double)Yaw * 3.14159265358979 / 180.0;
	const double Cos = std::cos(Radians), Sin = std::sin(Radians);
	const double OffsetX = Info.OffsetX * Sx, OffsetY = Info.OffsetY * Sy, OffsetZ = Info.OffsetZ * Sz, ExtentZ = Info.ExtentZ * Sz;
	const double RotatedX = OffsetX * Cos - OffsetY * Sin;
	const double RotatedY = OffsetX * Sin + OffsetY * Cos;

	auto Piece = SpawnPiece(Info.Class, CX - RotatedX, CY - RotatedY, BottomZ + ExtentZ - OffsetZ, Yaw, Builder, Team, Sx, Sy, Sz);

	if (!Piece)
		return nullptr;

	double OX, OY, OZ, EX, EY, EZ;

	if (!ActorBoundsOf(Piece, OX, OY, OZ, EX, EY, EZ))
		return Piece;

	++PlacedChecked;
	float UseYaw = Yaw;
	bool bChanged = false;

	// a panel's long side has to run along its edge: if the box says otherwise the piece is turned a quarter
	if (AlongX >= 0 && (std::max)(EX, EY) > 1.25 * (std::min)(EX, EY) && ((EX >= EY) != (AlongX == 1)))
	{
		UseYaw += 90.f;
		bChanged = true;
		++PlacedTurned;
		SetActorPlacement(Piece, (double)Piece->GetActorLocation().X, (double)Piece->GetActorLocation().Y, (double)Piece->GetActorLocation().Z, UseYaw);

		if (!ActorBoundsOf(Piece, OX, OY, OZ, EX, EY, EZ))
			return Piece;
	}

	// the middle of the real box has to be where it was wanted
	const double DX = CX - OX, DY = CY - OY, DZ = (BottomZ + EZ) - OZ;
	const double Shift = std::sqrt(DX * DX + DY * DY + DZ * DZ);

	if (Shift > 2.0 || bChanged)
	{
		const auto Now = Piece->GetActorLocation();
		SetActorPlacement(Piece, (double)Now.X + DX, (double)Now.Y + DY, (double)Now.Z + DZ, UseYaw);

		if (Shift > 2.0)
			++PlacedMoved;

		PlacedMaxShift = (std::max)(PlacedMaxShift, Shift);
	}

	return Piece;
}

// ---------------------------------------------------------------------------------------------------------------------
// The pieces a playset lists (class and how many), loaded and measured. The list says what and how many, not where.
// ---------------------------------------------------------------------------------------------------------------------
static void LoadPieces(UObject* Playset, const FVector& Probe, std::vector<PieceInfo>& Pieces, int& Failed, int& Skipped)
{
	Pieces.clear();
	Failed = 0;
	Skipped = 0;

	auto Counts = ReadClassCounts(Playset);
	static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");

	for (auto& Entry : Counts)
	{
		const std::wstring Path(Entry.Path.begin(), Entry.Path.end());
		UClass* Class = FindObject<UClass>(Path.c_str());

		if (!Class)
			Class = LoadObject<UClass>(Path.c_str(), ClassClass);

		if (!Class)
		{
			++Failed;
			continue;
		}

		PieceInfo Info;
		Info.Class = Class;
		Info.Name = Class->GetName();
		Info.Count = (std::min)(Entry.Count, 150);

		// not something to place: volumes, starts, controllers, game modes
		if (ContainsNoCase(Info.Name, "volume") || ContainsNoCase(Info.Name, "playerstart") || ContainsNoCase(Info.Name, "gamemode") || ContainsNoCase(Info.Name, "controller") ||
			ContainsNoCase(Info.Name, "pawn") || ContainsNoCase(Info.Name, "gamestate"))
		{
			++Skipped;
			continue;
		}

		Info.Kind = Architect::KindFromName(Info.Name);
		MeasurePiece(Info, Probe);
		Pieces.push_back(Info);
	}
}

static Architect::Request MakeRequest(UObject* Playset, const std::vector<PieceInfo>& Pieces)
{
	Architect::Request Request;
	Request.Name = Playset->GetName();

	const int SizeXOffset = Playset->GetOffset("SizeX", false);

	if (SizeXOffset != -1)
	{
		Request.SizeX = (std::max)(1, Playset->Get<int>(SizeXOffset));
		Request.SizeY = (std::max)(1, Playset->Get<int>(SizeXOffset + 4));
		Request.SizeZ = (std::max)(1, Playset->Get<int>(SizeXOffset + 8));
	}

	for (auto& P : Pieces)
	{
		Architect::Type Type;
		Type.Name = P.Name;
		Type.K = P.Kind;
		Type.Count = P.Count;
		Type.SX = 2 * P.ExtentX; Type.SY = 2 * P.ExtentY; Type.SZ = 2 * P.ExtentZ;
		Type.Sh = P.Shape;
		Type.HighAxis = P.HighAxis; Type.HighSign = P.HighSign; Type.RidgeAxis = P.RidgeAxis;
		Type.Measured = P.bMeasured;
		Request.Types.push_back(Type);
	}

	return Request;
}

// What the layout was given, in the form tools/PrefabPreview reads: pick the "Architect input: " lines out of the log and the prefab can be looked at
// (and the layout changed) without the game.
static void LogRequest(const Architect::Request& Request)
{
	LOG_INFO(LogCreative, "Architect input: prefab {} {} {} {}", Request.Name, Request.SizeX, Request.SizeY, Request.SizeZ);

	for (auto& T : Request.Types)
		LOG_INFO(LogCreative, "Architect input: type {} {} {} {:.0f} {:.0f} {:.0f} {} {} {} {}", T.Name, Architect::KindToken(T.K), T.Count, T.SX, T.SY, T.SZ,
			T.Sh == Architect::Shape::Shed ? "shed" : T.Sh == Architect::Shape::Tent ? "tent" : "box", T.HighAxis, T.HighSign, T.RidgeAxis);
}

// Returns how many pieces were built; 0 when the playset lists no pieces (the wood house is used then).
static int BuildPrefabFromClasses(AFortVolume* Volume, UObject* Playset)
{
	const int SizeXOffset = Playset->GetOffset("SizeX", false);

	if (SizeXOffset == -1)
		return 0;

	if (ReadClassCounts(Playset).empty())
	{
		LOG_INFO(LogCreative, "Own prefab (real pieces): {} lists no pieces", Playset->GetName());
		return 0;
	}

	const int PropsOffset = Playset->GetOffset("AssociatedPlaysetProps", false);
	const int Props = PropsOffset == -1 || !Readable((char*)Playset + PropsOffset, 16) ? 0 : *(int*)((char*)Playset + PropsOffset + 8);
	const int SizeX = Playset->Get<int>(SizeXOffset);
	const int SizeY = Playset->Get<int>(SizeXOffset + 4);

	// a device or another single thing: not a prefab
	if (Props <= 0 && SizeX * SizeY <= 1)
		return 0;

	const auto Center = Volume->GetActorLocation();
	const FVector Probe((float)Center.X, (float)Center.Y, (float)Center.Z + 12000.f);

	std::vector<PieceInfo> Pieces;
	int Failed = 0, Skipped = 0;
	LoadPieces(Playset, Probe, Pieces, Failed, Skipped);

	if (Pieces.empty())
	{
		LOG_WARN(LogCreative, "Own prefab (real pieces): none of the classes of {} could be used ({} could not be loaded, {} are not pieces)", Playset->GetName(), Failed, Skipped);
		return 0;
	}

	// the thrower builds the pieces (their team, the build animation)
	static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
	AFortPlayerControllerAthena* Builder = nullptr;
	{
		auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

		for (int i = 0; i < Controllers.Num() && !Builder; ++i)
		{
			auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);
			const int PlaysetOffset = Controller ? Controller->GetOffset("CurrentPlayset", false) : -1;

			if (Controller && PlaysetOffset != -1 && Controller->Get<UObject*>(PlaysetOffset) == Playset)
				Builder = Controller;
		}

		if (!Builder && Controllers.Num() > 0)
			Builder = (AFortPlayerControllerAthena*)Controllers.at(0);

		Controllers.Free();
	}

	int Team = -1;

	if (Builder)
		if (auto State = Cast<AFortPlayerStateAthena, false>(Builder->GetPlayerState()))
			Team = State->GetTeamIndex();

	const auto Request = MakeRequest(Playset, Pieces);
	LogRequest(Request);
	const auto Plan = Architect::Plan(Request);

	PlacedMoved = PlacedTurned = PlacedChecked = 0;
	PlacedMaxShift = 0;
	const double GroundZ = (double)Center.Z;
	int Built = 0;
	std::map<Architect::Kind, int> ByKind;

	for (auto& Item : Plan.Items)
	{
		if (Item.Type < 0 || Item.Type >= (int)Pieces.size())
			continue;

		const auto& Info = Pieces[Item.Type];

		if (PlaceBox(Info, (double)Center.X + Item.X, (double)Center.Y + Item.Y, GroundZ + Item.Z, (float)Item.Yaw, Item.AlongX, Builder, Team, Item.ScaleX, Item.ScaleY, Item.ScaleZ))
		{
			++Built;
			++ByKind[Plan.Types.size() > (size_t)Item.Type ? Plan.Types[Item.Type].K : Info.Kind];
		}
	}

	std::string Summary;

	for (auto& Entry : ByKind)
		Summary += std::format(" {}x{}", Entry.second, Architect::KindName(Entry.first));

	if (Plan.bExhibit)
	{
		LOG_INFO(LogCreative, "Own prefab (exhibit): built {} of {} planned pieces for {} (a gallery of props), {} classes loaded, {} could not be loaded:{}", Built, (int)Plan.Items.size(), Playset->GetName(), (int)Pieces.size(), Failed, Summary);
	}
	else
	{
		LOG_INFO(LogCreative, "Own prefab (architect): built {} of {} planned pieces for {} ({} x {} slots of {:.0f}, {} levels of {:.0f}, {} classes loaded, {} could not be loaded):{}", Built, (int)Plan.Items.size(), Playset->GetName(), Plan.CoreX,
			Plan.CoreY, Plan.Pitch, Plan.Levels, Plan.Step, (int)Pieces.size(), Failed, Summary);
	}

	for (auto& Note : Plan.Notes)
		LOG_INFO(LogCreative, "  layout: {}", Note);

	LOG_INFO(LogCreative, "Own prefab (architect): of {} pieces whose box was checked {} had to be moved (the largest by {:.0f}) and {} turned a quarter", PlacedChecked, PlacedMoved, PlacedMaxShift, PlacedTurned);

	for (auto& P : Pieces)
		LOG_INFO(LogCreative, "  piece {} x{} as {}: box {:.0f} x {:.0f} x {:.0f}, shape {}{}", P.Name, P.Count, Architect::KindName(P.Kind), 2 * P.ExtentX, 2 * P.ExtentY, 2 * P.ExtentZ,
			P.Shape == Architect::Shape::Shed ? "shed" : P.Shape == Architect::Shape::Tent ? "tent" : "box", P.bMeasured ? (P.bShaped ? "" : " (guessed)") : " (not measured)");

	return Built;
}

// Returns how many pieces were built (0 when the playset is not a building prefab).
static int BuildOwnPrefab(AFortVolume* Volume, UObject* Playset)
{
	const int SizeXOffset = Playset->GetOffset("SizeX", false);
	const int PropsOffset = Playset->GetOffset("AssociatedPlaysetProps", false);

	if (SizeXOffset == -1)
		return 0;

	int SizeX = Playset->Get<int>(SizeXOffset);
	int SizeY = Playset->Get<int>(SizeXOffset + 4);
	const int Props = PropsOffset == -1 || !Readable((char*)Playset + PropsOffset, 16) ? 0 : *(int*)((char*)Playset + PropsOffset + 8);

	// a device or another single thing: nothing to build a house for
	if (Props <= 0 && SizeX * SizeY <= 1)
	{
		LOG_INFO(LogCreative, "Own prefab: {} has no props and is {} x {}, so it is not a building", Playset->GetName(), SizeX, SizeY);
		return 0;
	}

	SizeX = (std::max)(2, (std::min)(SizeX, 10));
	SizeY = (std::max)(2, (std::min)(SizeY, 10));

	auto Wall = FindPieceClass("PBWA_W1_Solid_C", L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Solid.PBWA_W1_Solid_C");
	auto Floor = FindPieceClass("PBWA_W1_Floor_C", L"/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Floor.PBWA_W1_Floor_C");

	if (!Wall || !Floor)
	{
		LOG_WARN(LogCreative, "Own prefab: the wood wall ({}) or floor ({}) class could not be found", Wall ? "found" : "missing", Floor ? "found" : "missing");
		return 0;
	}

	// The thrower builds the pieces (their team, the build animation).
	static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
	AFortPlayerControllerAthena* Builder = nullptr;
	{
		auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

		for (int i = 0; i < Controllers.Num() && !Builder; ++i)
		{
			auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);
			const int PlaysetOffset = Controller ? Controller->GetOffset("CurrentPlayset", false) : -1;

			if (Controller && PlaysetOffset != -1 && Controller->Get<UObject*>(PlaysetOffset) == Playset)
				Builder = Controller;
		}

		if (!Builder && Controllers.Num() > 0)
			Builder = (AFortPlayerControllerAthena*)Controllers.at(0);

		Controllers.Free();
	}

	int Team = -1;

	if (Builder)
		if (auto State = Cast<AFortPlayerStateAthena, false>(Builder->GetPlayerState()))
			Team = State->GetTeamIndex();

	// Building pieces sit on a 256 grid and are 512 wide and 384 high; they are placed by a corner, a wall runs from its corner along its own +Y.
	const auto Center = Volume->GetActorLocation();
	const double OriginX = std::floor(((double)Center.X - SizeX * 256.0) / 256.0 + 0.5) * 256.0;
	const double OriginY = std::floor(((double)Center.Y - SizeY * 256.0) / 256.0 + 0.5) * 256.0;
	const double GroundZ = (double)Center.Z;
	const int Levels = 2;
	const double WallHeight = 384.0;
	const int DoorTile = SizeX / 2;

	int Walls = 0, Floors = 0;

	for (int Level = 0; Level < Levels; ++Level)
	{
		const double Z = GroundZ + Level * WallHeight;

		for (int i = 0; i < SizeX; ++i)
		{
			// the two edges that run along X (yaw 270 turns +Y into +X); the south one has a door gap on the ground level
			if (!(Level == 0 && i == DoorTile))
				Walls += SpawnPiece(Wall, OriginX + i * 512.0, OriginY, Z, 270.f, Builder, Team) != nullptr;

			Walls += SpawnPiece(Wall, OriginX + i * 512.0, OriginY + SizeY * 512.0, Z, 270.f, Builder, Team) != nullptr;
		}

		for (int j = 0; j < SizeY; ++j)
		{
			Walls += SpawnPiece(Wall, OriginX, OriginY + j * 512.0, Z, 0.f, Builder, Team) != nullptr;
			Walls += SpawnPiece(Wall, OriginX + SizeX * 512.0, OriginY + j * 512.0, Z, 0.f, Builder, Team) != nullptr;
		}
	}

	// the roof: a floor piece over every tile
	for (int i = 0; i < SizeX; ++i)
		for (int j = 0; j < SizeY; ++j)
			Floors += SpawnPiece(Floor, OriginX + i * 512.0, OriginY + j * 512.0, GroundZ + Levels * WallHeight, 0.f, Builder, Team) != nullptr;

	LOG_INFO(LogCreative, "Own prefab: built {} walls and {} roof floors for {} ({} x {} tiles, {} props in the real one) from {} {} {}, builder {}, team {}", Walls, Floors, Playset->GetName(), SizeX, SizeY, Props,
		OriginX, OriginY, GroundZ, Describe(Builder), Team);

	return Walls + Floors;
}

// ---------------------------------------------------------------------------------------------------------------------
// Our own devices. The playsets of the devices (PID_CP_Devices_<Name>) point at nothing, but the asset registry lists a map CP_Devices_<Name> and
// the device's blueprints under /Game/Creative/Devices/<Name> for each of them. The device's actor class is looked up there and spawned in the
// volume the grenade made.
// ---------------------------------------------------------------------------------------------------------------------
// Everything the asset registry lists under a package path, as (package name, asset class).
static std::vector<std::pair<std::string, std::string>> RegistryAssets(const std::string& PackagePath, bool bRecursive)
{
	std::vector<std::pair<std::string, std::string>> Result;

	static auto Helpers = FindObject<UObject>(L"/Script/AssetRegistry.Default__AssetRegistryHelpers");
	auto GetRegistryFn = Helpers ? Helpers->FindFunction("GetAssetRegistry") : nullptr;

	if (!GetRegistryFn)
		return Result;

	struct { UObject* Object; void* Interface; } Registry{};

	if (!SafeProcessEvent(Helpers, GetRegistryFn, &Registry) || !PropDump::IsLiveObject(Registry.Object))
		return Result;

	auto ByPathFn = Registry.Object->FindFunction("GetAssetsByPath");

	if (!ByPathFn)
		return Result;

	const std::wstring Wide(PackagePath.begin(), PackagePath.end());

	struct { FName PackagePath; TArray<char> OutAssetData; bool bRecursive; bool bIncludeOnlyOnDiskAssets; bool ReturnValue; } Params{};
	Params.PackagePath = UKismetStringLibrary::Conv_StringToName(Wide.c_str());
	Params.bRecursive = bRecursive;
	Params.bIncludeOnlyOnDiskAssets = false;

	if (!SafeProcessEvent(Registry.Object, ByPathFn, &Params))
		return Result;

	for (int i = 0; i < Params.OutAssetData.Num() && i < 5000; ++i)
	{
		const char* Asset = (const char*)Params.OutAssetData.GetData() + (size_t)i * 0x50;

		if (!Readable(Asset, 0x50))
			break;

		Result.push_back({ NameAt(Asset + 0x08), NameAt(Asset + 0x20) });
	}

	return Result;
}

static bool InheritsFrom(UClass* Class, UClass* Base)
{
	int Guard = 0;

	for (auto S = (UStruct*)Class; S && Guard < 64; S = S->GetSuperStruct(), ++Guard)
		if (S == (UStruct*)Base)
			return true;

	return false;
}

// The actor class of a device: a blueprint in the device's folder that is a building actor.
static UClass* FindDeviceClass(const std::string& Device)
{
	static auto BuildingActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingActor");
	static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");
	static std::map<std::string, UClass*> Cache;

	if (auto It = Cache.find(Device); It != Cache.end() && It->second)
		return It->second;

	auto Assets = RegistryAssets("/Game/Creative/Devices/" + Device, true);

	// PID_CP_Devices_CreativeButton has its blueprints in .../Devices/Button
	if (Assets.empty() && Device.rfind("Creative", 0) == 0 && Device.size() > 8)
		Assets = RegistryAssets("/Game/Creative/Devices/" + Device.substr(8), true);

	std::vector<std::string> Candidates;

	for (auto& [Package, Class] : Assets)
		if (Class == "Blueprint")
			Candidates.push_back(Package);

	LOG_INFO(LogCreative, "Own device {}: the registry lists {} assets under /Game/Creative/Devices/{}, {} of them blueprints", Device, (int)Assets.size(), Device, (int)Candidates.size());

	auto NameOf = [](const std::string& Package) { return Package.substr(Package.find_last_of('/') + 1); };

	// the likely actor blueprints first (BP_<Name>, <Name>_GBA)
	std::stable_sort(Candidates.begin(), Candidates.end(), [&](const std::string& A, const std::string& B) {
		auto Score = [&](const std::string& Package) {
			const auto Name = NameOf(Package);
			return (Name.rfind("BP_", 0) == 0 ? 2 : 0) + (Name.size() > 4 && Name.compare(Name.size() - 4, 4, "_GBA") == 0 ? 2 : 0) + (Name.find(Device) != std::string::npos ? 1 : 0);
		};
		return Score(A) > Score(B);
	});

	for (auto& Package : Candidates)
	{
		const auto Name = NameOf(Package);

		// abilities, effects, cues and the like are not the device
		if (Name.rfind("GA_", 0) == 0 || Name.rfind("GE_", 0) == 0 || Name.rfind("GC_", 0) == 0 || Name.rfind("GCN_", 0) == 0 || Name.rfind("W_", 0) == 0 || Name.rfind("WBP_", 0) == 0)
			continue;

		const std::string PathText = Package + "." + Name + "_C";
		const std::wstring Path(PathText.begin(), PathText.end());

		UClass* Class = FindObject<UClass>(Path.c_str());

		if (!Class)
			Class = LoadObject<UClass>(Path.c_str(), ClassClass);

		LOG_INFO(LogCreative, "Own device {}: candidate {} -> {}{}", Device, PathText, Class ? "loaded" : "could not be loaded", Class && !InheritsFrom(Class, BuildingActorClass) ? " (not a building actor)" : "");

		if (Class && InheritsFrom(Class, BuildingActorClass))
		{
			Cache[Device] = Class;
			return Class;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Devices from their map. Every device has a map CP_Devices_<Name> (the registry lists them) that holds what the real game puts in the volume. The map
// is loaded as an asset only (not streamed into the world), its actors are read (class and placement) and the same actors are spawned in the volume.
// ---------------------------------------------------------------------------------------------------------------------
static UObject* SafeLoadAsset(UClass* Class, const wchar_t* Path)
{
	__try
	{
		return StaticLoadObjectOriginal ? (UObject*)StaticLoadObjectOriginal(Class, nullptr, Path, nullptr, 0, nullptr, false) : nullptr;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The creative HUD of a player: the phone's quickbar and the menus behind it. The SDK shows what the client needs from the server: the controller's
// replicated flags bIsCreativeModeEnabled and bIsCreativeQuickbarEnabled (both repnotify) and its replicated CreativeQuickbarComponent, which the game makes
// from the CreativeQuickbarClass it holds. Nothing in this project ever set the flags or checked the component, so the phone could be in the inventory
// and the client still had no creative HUD to put it in. This writes down what the controller really has, and fills in what is missing.
// ---------------------------------------------------------------------------------------------------------------------
static UObject* QuickbarOf(UObject* Controller)
{
	const int Offset = Controller ? Controller->GetOffset("CreativeQuickbarComponent", false) : -1;
	return Offset == -1 ? nullptr : Controller->Get<UObject*>(Offset);
}

static void CheckCreativeHud(AFortPlayerControllerAthena* Controller, const char* When, bool bFix)
{
	if (!Controller)
		return;

	struct HudFlag
	{
		const char* Name;
		const char* OnRep;
		bool bOnRepGetsPrevious;
	};

	static const HudFlag Flags[] = {
		{ "bIsCreativeModeEnabled", "OnRep_IsCreativeModeEnabled", false },
		{ "bIsCreativeQuickbarEnabled", "OnRep_IsCreativeQuickbarEnabled", true },
		{ "bIsCreativeQuickmenuEnabled", nullptr, false },
	};

	std::string Text;

	for (auto& Flag : Flags)
	{
		const int Offset = Controller->GetOffset(Flag.Name, false);
		void* Property = Offset == -1 ? nullptr : Controller->GetProperty(Flag.Name, false);

		if (!Property)
		{
			Text += std::format("{} n/a; ", Flag.Name);
			continue;
		}

		const uint8_t Mask = GetFieldMask(Property);
		const bool bOn = Controller->ReadBitfieldValue(Offset, Mask);

		if (bOn || !bFix)
		{
			Text += std::format("{} {}; ", Flag.Name, bOn ? "on" : "OFF");
			continue;
		}

		Controller->SetBitfieldValue(Offset, Mask, true);

		if (Flag.OnRep)
			if (auto Function = Controller->FindFunction(Flag.OnRep))
			{
				bool bPrevious = false;
				SafeProcessEvent(Controller, Function, Flag.bOnRepGetsPrevious ? &bPrevious : nullptr);
			}

		Text += std::format("{} was OFF, set; ", Flag.Name);
	}

	LOG_INFO(LogCreative, "Creative HUD ({}) of {}: {}", When, Controller->GetName(), Text);

	if (!bFix)
		return;

	auto Quickbar = QuickbarOf(Controller);

	if (Quickbar)
	{
		LOG_INFO(LogCreative, "Creative HUD ({}): the quickbar component is {}", When, Describe(Quickbar));
		return;
	}

	// the game's own callback for the phone (it may make the component)
	if (auto Callback = Controller->FindFunction("GrantCreativePhoneItemDefLoadedCallback"))
		SafeProcessEvent(Controller, Callback, nullptr);

	Quickbar = QuickbarOf(Controller);

	if (Quickbar)
	{
		LOG_INFO(LogCreative, "Creative HUD ({}): the game's callback made the quickbar component {}", When, Describe(Quickbar));
		return;
	}

	// otherwise it is made here, from the class the controller holds
	const int ClassOffset = Controller->GetOffset("CreativeQuickbarClass", false);
	UClass* Class = nullptr;
	std::string ClassPath = "n/a";

	if (ClassOffset != -1)
	{
		auto& Soft = Controller->Get<TSoftObjectPtr<UObject>>(ClassOffset);

		if (Soft.IsValid())
		{
			ClassPath = Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString();
			const std::wstring Path(ClassPath.begin(), ClassPath.end());
			static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");
			Class = (UClass*)FindObject<UObject>(Path.c_str());

			if (!Class)
				Class = (UClass*)SafeLoadAsset(ClassClass, Path.c_str());
		}
		else
			ClassPath = "not set";
	}

	if (!Class)
	{
		LOG_WARN(LogCreative, "Creative HUD ({}): {} has no quickbar component and its quickbar class ({}) could not be loaded", When, Controller->GetName(), ClassPath);
		return;
	}

	auto Made = UGameplayStatics::SpawnObject(Class, Controller);

	if (!Made)
	{
		LOG_WARN(LogCreative, "Creative HUD ({}): making the quickbar component from {} failed", When, ClassPath);
		return;
	}

	Controller->Get<UObject*>(Controller->GetOffset("CreativeQuickbarComponent")) = Made;

	if (auto Replicate = Made->FindFunction("SetIsReplicated"))
	{
		bool bReplicate = true;
		SafeProcessEvent(Made, Replicate, &bReplicate);
	}

	CreativeIsland::CallOnRep(Controller, "OnRep_CreativeQuickbarComponent");
	Controller->ForceNetUpdate();
	LOG_INFO(LogCreative, "Creative HUD ({}): made the quickbar component {} from {}", When, Describe(Made), ClassPath);
	PropDump::DumpObject("Quickbar component", Made, 1, 40);
}

// Where a player starts on an island. The 117 FortPlayerStartCreative actors of the world are the hub's (the lobby island floats above the plots: the 7 Nov log picked one
// at Z 26064 for an island whose ground is at about 1990), so a start counts only at the height of the island (within 4000 of its ground). With none, the player
// stands on the ground in the middle of the island (a downward trace from above it); the sky drop is the last resort. The log says what was found.
static bool FindIslandStart(AFortVolume* Volume, FVector& OutLocation, FRotator& OutRotation)
{
	static auto StartClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerStartCreative");

	if (!Volume)
		return false;

	const auto Center = Volume->GetActorLocation();
	double GroundZ = 0;
	const bool bGround = TraceDown((double)Center.X, (double)Center.Y, (double)Center.Z + 13000.0, (double)Center.Z - 2000.0, false, GroundZ); // from 13000 above the volume's origin (the island's ground is 6000 above it)

	AActor* Best = nullptr;
	double BestScore = 0;
	int Total = 0, Near = 0, Inside = 0;

	if (StartClass)
	{
		auto Starts = UGameplayStatics::GetAllActorsOfClass(GetWorld(), StartClass);
		Total = Starts.Num();

		for (int i = 0; i < Starts.Num(); ++i)
		{
			auto Start = Starts.at(i);

			if (!Start)
				continue;

			const auto At = Start->GetActorLocation();
			const double Dx = (double)At.X - (double)Center.X, Dy = (double)At.Y - (double)Center.Y;
			const double Distance = std::sqrt(Dx * Dx + Dy * Dy);

			if (Distance > 30000.0)
				continue;

			++Near;

			// at the height of the island
			if (bGround && std::fabs((double)At.Z - GroundZ) > 4000.0)
				continue;

			static auto EnabledOffset = Start->GetOffset("bIsEnabled", false);
			static auto IslandStartOffset = Start->GetOffset("bUseAsIslandStart", false);

			if (EnabledOffset != -1 && !Start->Get<bool>(EnabledOffset))
				continue;

			++Inside;
			const bool bIslandStart = IslandStartOffset != -1 && Start->Get<bool>(IslandStartOffset);
			const double Score = Distance - (bIslandStart ? 100000.0 : 0.0);

			if (!Best || Score < BestScore)
			{
				Best = Start;
				BestScore = Score;
			}
		}

		Starts.Free();
	}

	if (Best)
	{
		OutLocation = Best->GetActorLocation();
		OutLocation.Z += 120.f;
		OutRotation = Best->GetActorRotation();
		LOG_INFO(LogCreative, "Island start: {} of {} FortPlayerStartCreative actors are at the island's height ({} within reach), using {} at {}", Inside, Total, Near, Describe(Best), Where(Best));
		return true;
	}

	LOG_INFO(LogCreative, "Island start: {} FortPlayerStartCreative actors exist, {} within 30000 of the island at {} {}, none at its height (ground {})", Total, Near, Center.X, Center.Y, bGround ? std::format("{:.0f}", GroundZ) : std::string("not found"));

	if (!bGround)
		return false;

	OutLocation = FVector(Center.X, Center.Y, (float)GroundZ + 150.f);
	OutRotation = FRotator();
	LOG_INFO(LogCreative, "Island start: the player stands on the ground in the middle of the island, at {:.0f} {:.0f} {:.0f}", (double)OutLocation.X, (double)OutLocation.Y, (double)OutLocation.Z);
	return true;
}

struct MapActor
{
	UClass* Class = nullptr;
	double X = 0, Y = 0, Z = 0;
	float Pitch = 0, Yaw = 0, Roll = 0;
	double ScaleX = 1, ScaleY = 1, ScaleZ = 1;
};

static std::string LowerText(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Text;
}

// Every device map the game has (the asset registry survey listed these 47 in the 6 Nov log). The registry helper below returns nothing when it is asked at
// the wrong moment, and a device whose playset is not named like its map (PID_CP_Devices_CreativeButton: CP_Devices_Button; the radio's map is in its own
// folder and called CP_Device_Radio) could not be found without it.
static const char* const KnownDeviceMaps[] = {
	"/Game/Creative/Devices/Radio/CP_Device_Radio",
	"/Game/Creative/Maps/Devices/CP_Devices_AdvStormController",
	"/Game/Creative/Maps/Devices/CP_Devices_BasicStormController",
	"/Game/Creative/Maps/Devices/CP_Devices_Billboard",
	"/Game/Creative/Maps/Devices/CP_Devices_BoulderSpawner",
	"/Game/Creative/Maps/Devices/CP_Devices_Button",
	"/Game/Creative/Maps/Devices/CP_Devices_CaptureArea",
	"/Game/Creative/Maps/Devices/CP_Devices_ChannelSwitchboard",
	"/Game/Creative/Maps/Devices/CP_Devices_ClassSelector",
	"/Game/Creative/Maps/Devices/CP_Devices_ClassSettings",
	"/Game/Creative/Maps/Devices/CP_Devices_ColorChangeTile",
	"/Game/Creative/Maps/Devices/CP_Devices_CoolCarpet",
	"/Game/Creative/Maps/Devices/CP_Devices_CreatureManager",
	"/Game/Creative/Maps/Devices/CP_Devices_CreaturePlacer",
	"/Game/Creative/Maps/Devices/CP_Devices_DeimosSpawner",
	"/Game/Creative/Maps/Devices/CP_Devices_DestroyTarget",
	"/Game/Creative/Maps/Devices/CP_Devices_EliminationManager",
	"/Game/Creative/Maps/Devices/CP_Devices_ExplosiveBarrel",
	"/Game/Creative/Maps/Devices/CP_Devices_HUDMessage",
	"/Game/Creative/Maps/Devices/CP_Devices_HoloScreen",
	"/Game/Creative/Maps/Devices/CP_Devices_Hover_Platform",
	"/Game/Creative/Maps/Devices/CP_Devices_ItemGranter",
	"/Game/Creative/Maps/Devices/CP_Devices_ItemSpawner",
	"/Game/Creative/Maps/Devices/CP_Devices_ItemSpawnerProp",
	"/Game/Creative/Maps/Devices/CP_Devices_ItemSpawnerVendingMachine",
	"/Game/Creative/Maps/Devices/CP_Devices_Jet",
	"/Game/Creative/Maps/Devices/CP_Devices_KeyLock",
	"/Game/Creative/Maps/Devices/CP_Devices_LockDevice",
	"/Game/Creative/Maps/Devices/CP_Devices_MapMarker",
	"/Game/Creative/Maps/Devices/CP_Devices_MatchmakingPortal",
	"/Game/Creative/Maps/Devices/CP_Devices_PerceptionTrigger",
	"/Game/Creative/Maps/Devices/CP_Devices_PinballBumper",
	"/Game/Creative/Maps/Devices/CP_Devices_PinballFlipper",
	"/Game/Creative/Maps/Devices/CP_Devices_PowerUp",
	"/Game/Creative/Maps/Devices/CP_Devices_PropPossessorConfig",
	"/Game/Creative/Maps/Devices/CP_Devices_Quest",
	"/Game/Creative/Maps/Devices/CP_Devices_RNG",
	"/Game/Creative/Maps/Devices/CP_Devices_RoundSettings",
	"/Game/Creative/Maps/Devices/CP_Devices_ScoreManager",
	"/Game/Creative/Maps/Devices/CP_Devices_ScreenshotCam",
	"/Game/Creative/Maps/Devices/CP_Devices_Speaker",
	"/Game/Creative/Maps/Devices/CP_Devices_StormBeacon",
	"/Game/Creative/Maps/Devices/CP_Devices_SwordStone",
	"/Game/Creative/Maps/Devices/CP_Devices_TVScreen",
	"/Game/Creative/Maps/Devices/CP_Devices_Teleporter",
	"/Game/Creative/Maps/Devices/CP_Devices_TimerObjective",
	"/Game/Creative/Maps/Devices/CP_Devices_Trigger",
};

static std::vector<MapActor> ReadDeviceMap(const std::string& Device)
{
	static std::map<std::string, std::vector<MapActor>> Cache;

	if (auto It = Cache.find(Device); It != Cache.end())
		return It->second;

	std::vector<MapActor>& Result = Cache[Device];
	static auto WorldClass = FindObject<UClass>(L"/Script/Engine.World");

	// the map: CP_Devices_<Name> in the devices map folder (listed once, recursively: listing that folder on its own came back empty)
	static std::vector<std::pair<std::string, std::string>> DeviceMaps;
	static bool bListed = false;

	if (!bListed)
	{
		bListed = true;

		for (auto& Entry : RegistryAssets("/Game/Creative", true))
			if (Entry.second == "World" && Entry.first.find("/CP_Device") != std::string::npos)
				DeviceMaps.push_back(Entry);

		LOG_INFO(LogCreative, "Device maps: the registry lists {} device maps", (int)DeviceMaps.size());

		// the known ones are added (the registry may have given none)
		for (const char* Known : KnownDeviceMaps)
		{
			bool bHave = false;

			for (auto& Entry : DeviceMaps)
				bHave = bHave || Entry.first == Known;

			if (!bHave)
				DeviceMaps.push_back({ Known, "World" });
		}

		LOG_INFO(LogCreative, "Device maps: {} in all, with the known ones", (int)DeviceMaps.size());
	}

	// Every device's map is named like this; the registry listing is only the fallback for a map that is named otherwise.
	auto TryLoad = [&](const std::string& Package, std::string& OutPath) -> UObject* {
		OutPath = Package + "." + Package.substr(Package.find_last_of('/') + 1);
		const std::wstring Path(OutPath.begin(), OutPath.end());
		UObject* Loaded = FindObject<UObject>(Path.c_str());

		if (!Loaded)
			Loaded = SafeLoadAsset(WorldClass, Path.c_str());

		return Loaded;
	};

	std::string PathText;
	UObject* World = TryLoad("/Game/Creative/Maps/Devices/CP_Devices_" + Device, PathText);

	if (!World)
	{
		LOG_INFO(LogCreative, "Device map {}: {} did not load, looking in the registry's list", Device, PathText);

		// lower case, no underscores, no "creative" in front: CreativeButton, Creative_Button and Button are the same name
		auto Plain = [](std::string Text) {
			Text = LowerText(Text);
			Text.erase(std::remove(Text.begin(), Text.end(), '_'), Text.end());

			if (Text.rfind("cpdevices", 0) == 0)
				Text = Text.substr(9);
			else if (Text.rfind("cpdevice", 0) == 0)
				Text = Text.substr(8);

			if (Text.rfind("creative", 0) == 0 && Text.size() > 8)
				Text = Text.substr(8);

			return Text;
		};

		auto Wanted = Plain(Device);

		// names that really differ
		for (auto& Alias : { std::pair<const char*, const char*>{ "randomnumbergenerator", "rng" }, { "screenshotcamera", "screenshotcam" }, { "propposessorconfigurator", "proppossessorconfig" },
			{ "proppossessorconfigurator", "proppossessorconfig" }, { "elimination", "eliminationmanager" } })
			if (Wanted == Alias.first)
				Wanted = Alias.second;

		for (auto& [Name, Class] : DeviceMaps)
		{
			const auto Short = Plain(Name.substr(Name.find_last_of('/') + 1));
			const bool bSame = Short == Wanted || (Wanted.size() >= 4 && Short.size() >= 4 && (Short.find(Wanted) != std::string::npos || Wanted.find(Short) != std::string::npos));

			if (bSame && (World = TryLoad(Name, PathText)))
			{
				LOG_INFO(LogCreative, "Device map {}: found as {}", Device, PathText);
				break;
			}
		}
	}

	LOG_INFO(LogCreative, "Device map {}: {} {}", Device, PathText, World ? "loaded" : "COULD NOT BE LOADED");

	if (!World)
		return Result;

	const int LevelOffset = World->GetOffset("PersistentLevel", false);
	UObject* Level = LevelOffset == -1 ? nullptr : World->Get<UObject*>(LevelOffset);

	if (!PropDump::IsLiveObject(Level))
	{
		LOG_WARN(LogCreative, "Device map {}: the map has no persistent level", Device);
		return Result;
	}

	// ULevel::Actors, at 0x98 in this build's dump (not a reflected property)
	constexpr int ActorsAt = 0x98;

	if (!Readable((char*)Level + ActorsAt, 16))
		return Result;

	AActor** Actors = *(AActor***)((char*)Level + ActorsAt);
	const int Num = *(int*)((char*)Level + ActorsAt + 8);

	LOG_INFO(LogCreative, "Device map {}: the level holds {} actors", Device, Num);

	if (!Actors || Num <= 0 || Num > 5000 || !Readable(Actors, (size_t)Num * 8))
		return Result;

	for (int i = 0; i < Num; ++i)
	{
		auto Actor = Actors[i];

		if (!PropDump::IsLiveObject(Actor) || !PropDump::IsLiveObject(Actor->ClassPrivate))
			continue;

		const std::string ClassText = Actor->ClassPrivate->GetFullName();
		const bool bGameClass = ClassText.find(" /Game/") != std::string::npos;

		const int RootOffset = Actor->GetOffset("RootComponent", false);
		UObject* Root = RootOffset == -1 ? nullptr : Actor->Get<UObject*>(RootOffset);

		MapActor Entry;
		Entry.Class = Actor->ClassPrivate;

		if (PropDump::IsLiveObject(Root))
		{
			const int LocationOffset = Root->GetOffset("RelativeLocation", false);
			const int RotationOffset = Root->GetOffset("RelativeRotation", false);
			const int ScaleOffset = Root->GetOffset("RelativeScale3D", false);

			if (LocationOffset != -1 && Readable((char*)Root + LocationOffset, 12))
			{
				const float* L = (const float*)((char*)Root + LocationOffset);
				Entry.X = L[0]; Entry.Y = L[1]; Entry.Z = L[2];
			}

			if (RotationOffset != -1 && Readable((char*)Root + RotationOffset, 12))
			{
				const float* R = (const float*)((char*)Root + RotationOffset);
				Entry.Pitch = R[0]; Entry.Yaw = R[1]; Entry.Roll = R[2];
			}

			if (ScaleOffset != -1 && Readable((char*)Root + ScaleOffset, 12))
			{
				const float* S = (const float*)((char*)Root + ScaleOffset);
				Entry.ScaleX = S[0]; Entry.ScaleY = S[1]; Entry.ScaleZ = S[2];
			}
		}

		LOG_INFO(LogCreative, "  map actor {}: {} at {:.0f} {:.0f} {:.0f}, rotation {:.0f} {:.0f} {:.0f}, scale {:.2f} {:.2f} {:.2f}{}", i, ClassText, Entry.X, Entry.Y, Entry.Z, Entry.Pitch, Entry.Yaw, Entry.Roll, Entry.ScaleX,
			Entry.ScaleY, Entry.ScaleZ, bGameClass ? "" : " (an engine class, not spawned)");

		// only the game's own blueprints are devices; the engine's actors (world settings, lights, brushes) are not
		if (bGameClass)
			Result.push_back(Entry);
	}

	return Result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings. A device's options and the island's settings are the same thing in this game: a FortActorOptionsComponent on the actor (the island's
// settings machine has a ToyOptionsComponent, which is one), holding the changed options as overrides (PlayerOptionData, replicated). The client's
// options screen changes them with a request to the server: ServerUpdateActorOptions(actor, keys, values) for a device, ServerUpdateGameplayOptions for
// the island's game settings, ServerUpdateMessageComponents for message devices (billboard, HUD message). The request handlers are the game's own
// native code. They were never visible in the log before, because the log filter threw away every function called ServerUpdate...
//
// What this does: writes every such request (what was asked, what the options were before and are after), checks that the game's handler really set
// them, and when it did not sets them itself through the options component's own functions (SetPropertyOverrideValues, SetPropertyOverrides).
// ---------------------------------------------------------------------------------------------------------------------
namespace CreativeSettings
{
	enum { None = 0, ActorOptions, GameplayOptions, ItemListOptions, MessageComponents, GameDescription, Interact, DeviceButton, MoveDuplicate, MoveCut, MoveSpawn, KindCount };

	struct Option
	{
		std::string Key, Value;
	};

	int KindOf(UFunction* Function)
	{
		static UFunction* Functions[KindCount] = {};
		static const bool bLooked = [&]() {
			Functions[ActorOptions] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerUpdateActorOptions");
			Functions[GameplayOptions] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerUpdateGameplayOptions");
			Functions[ItemListOptions] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerUpdateItemListOptions");
			Functions[MessageComponents] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerUpdateMessageComponents");
			Functions[GameDescription] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerUpdateGameDescriptionData");
			Functions[Interact] = FindObject<UFunction>(L"/Script/FortniteGame.FortControllerComponent_Interaction.ServerAttemptInteract");
						Functions[DeviceButton] = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerController.ServerBroadcastDeviceButtonComponentClickedDelegate");
			Functions[MoveDuplicate] = FindObject<UFunction>(L"/Script/FortniteGame.FortCreativeMoveTool.ServerDuplicateStartInteracting");
			Functions[MoveCut] = FindObject<UFunction>(L"/Script/FortniteGame.FortCreativeMoveTool.ServerHandshakeFromClientForReceivingCutActors");
			Functions[MoveSpawn] = FindObject<UFunction>(L"/Script/FortniteGame.FortCreativeMoveTool.ServerSpawnSelectedActorsWithTransform");

			return true;
		}();

		(void)bLooked;

		if (!Function)
			return None;

		for (int Kind = 1; Kind < KindCount; ++Kind)
			if (Functions[Kind] == Function)
				return Kind;

		return None;
	}

	static std::string TextOf(const FString& Text)
	{
		return Text.Data.Data && Readable(Text.Data.Data, 2) ? Text.ToString() : std::string();
	}

	static std::vector<Option> ReadOptions(const TArray<FString>& Keys, const TArray<FString>& Values)
	{
		std::vector<Option> Result;

		for (int i = 0; i < Keys.Num() && i < 200; ++i)
			Result.push_back({ TextOf(Keys.at(i)), i < Values.Num() ? TextOf(Values.at(i)) : std::string() });

		return Result;
	}

	// A TMap<FString, FString> read from raw memory (the same layout as the playset's class counts: a sparse array of 0x28 byte entries, the key
	// at +0 and the value at +0x10).
	static std::vector<Option> ReadStringMap(const char* Map)
	{
		std::vector<Option> Result;
		const int Count = MapCount(Map);

		if (Count <= 0 || Count > 5000 || !Readable(Map, 0x38))
			return Result;

		const char* Data = *(const char* const*)Map;
		const int Slots = *(const int*)(Map + 8);
		const int NumBits = *(const int*)(Map + 0x28);
		const unsigned* Flags = NumBits > 128 ? *(const unsigned* const*)(Map + 0x20) : (const unsigned*)(Map + 0x10);

		if (!Data || Slots <= 0 || Slots > 100000 || !Readable(Flags, (size_t)((Slots + 31) / 32) * 4))
			return Result;

		for (int i = 0; i < Slots; ++i)
		{
			if (!(Flags[i / 32] & (1u << (i % 32))))
				continue;

			const char* Entry = Data + (size_t)i * 0x28;

			if (!Readable(Entry, 0x28))
				break;

			Result.push_back({ TextOf(*(const FString*)Entry), TextOf(*(const FString*)(Entry + 0x10)) });
		}

		return Result;
	}

	// An FString in memory the engine allocator gave out (so that whatever frees it later may).
	static void MakeString(char* Destination, const std::string& Text)
	{
		const std::wstring Wide(Text.begin(), Text.end());
		const int Num = (int)Wide.size() + 1;
		auto Buffer = (wchar_t*)FMemory::Realloc(nullptr, (size_t)Num * sizeof(wchar_t), 0);
		memcpy(Buffer, Wide.c_str(), (size_t)Num * sizeof(wchar_t));
		*(wchar_t**)Destination = Buffer;
		*(int*)(Destination + 8) = Num;
		*(int*)(Destination + 12) = Num;
	}

	// A TMap<FString, FString> built by hand: one hash bucket (every lookup walks the chain, which is correct for any hash), up to 100 entries.
	static void BuildStringMap(char* Map, const std::vector<Option>& Entries)
	{
		memset(Map, 0, 0x50);
		const int N = (int)(std::min)(Entries.size(), (size_t)100);

		if (N == 0)
			return;

		auto Data = (char*)FMemory::Realloc(nullptr, (size_t)N * 0x28, 0);
		memset(Data, 0, (size_t)N * 0x28);

		for (int i = 0; i < N; ++i)
		{
			char* Entry = Data + (size_t)i * 0x28;
			MakeString(Entry, Entries[i].Key);
			MakeString(Entry + 0x10, Entries[i].Value);
			*(int*)(Entry + 0x20) = i - 1; // the next entry in the same bucket
			*(int*)(Entry + 0x24) = 0; // the bucket
		}

		*(char**)Map = Data;
		*(int*)(Map + 0x08) = N;
		*(int*)(Map + 0x0C) = N;

		auto Words = (unsigned*)(Map + 0x10);

		for (int i = 0; i < N; ++i)
			Words[i / 32] |= 1u << (i % 32);

		*(int*)(Map + 0x28) = N; // bits used
		*(int*)(Map + 0x2C) = 128; // bits the inline allocation holds
		*(int*)(Map + 0x30) = -1; // no free entries
		*(int*)(Map + 0x34) = 0;
		*(int*)(Map + 0x38) = N - 1; // the one bucket points at the last entry, which links back to the others
		*(int*)(Map + 0x48) = 1; // the hash has one bucket
	}

	static UObject* OptionsComponentOf(UObject* Actor)
	{
		static auto ComponentClass = FindObject<UClass>(L"/Script/FortniteGame.FortActorOptionsComponent");

		if (!Actor || !ComponentClass || !PropDump::IsLiveObject(Actor))
			return nullptr;

		return (UObject*)((AActor*)Actor)->GetComponentByClass(ComponentClass);
	}

	static bool CurrentOptions(UObject* Component, std::vector<Option>& Out)
	{
		auto Function = Component ? Component->FindFunction("GetPropertyOverrides") : nullptr;

		if (!Function)
			return false;

		struct { char Map[0x50]; bool ReturnValue; char Pad[7]; } Params{};

		if (!SafeProcessEvent(Component, Function, &Params))
			return false;

		Out = ReadStringMap(Params.Map);
		return true;
	}

	// Sets options on a component with its own functions; the one that only changes values first.
	static bool ApplyOptions(UObject* Component, const std::vector<Option>& Wanted)
	{
		for (const char* Name : { "SetPropertyOverrideValues", "SetPropertyOverrides" })
		{
			auto Function = Component->FindFunction(Name);

			if (!Function)
				continue;

			struct { char Map[0x50]; bool ReturnValue; char Pad[7]; } Params{};
			BuildStringMap(Params.Map, Wanted);
			const bool bRan = SafeProcessEvent(Component, Function, &Params);
			LOG_INFO(LogCreative, "Settings: {} on {} {} and returned {}", Name, Describe(Component), bRan ? "ran" : "FAILED (exception)", Params.ReturnValue);

			if (bRan && Params.ReturnValue)
				return true;
		}

		return false;
	}

	static int Matching(const std::vector<Option>& Wanted, const std::vector<Option>& Now)
	{
		int Count = 0;

		for (auto& W : Wanted)
			for (auto& N : Now)
				if (N.Key == W.Key && N.Value == W.Value)
				{
					++Count;
					break;
				}

		return Count;
	}

	static std::string Summary(const std::vector<Option>& Options, size_t Max = 14)
	{
		std::string Text;

		for (size_t i = 0; i < Options.size() && i < Max; ++i)
			Text += std::format("{}{}={}", i ? ", " : "", Options[i].Key, Options[i].Value);

		if (Options.size() > Max)
			Text += std::format(", ... ({} more)", (int)(Options.size() - Max));

		return Text;
	}

	static UObject* ControllerPawn(UObject* Controller)
	{
		static int PawnOffset = -2; // not looked up yet

		if (PawnOffset == -2 && Controller)
			PawnOffset = Controller->GetOffset("Pawn", false);

		return Controller && PawnOffset >= 0 ? Controller->Get<UObject*>(PawnOffset) : nullptr;
	}

	static UObject* FirstController()
	{
		static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
		UObject* Found = nullptr;

		if (ControllerClass)
		{
			auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);
			Found = Controllers.Num() > 0 ? Controllers.at(0) : nullptr;
			Controllers.Free();
		}

		return Found;
	}

	// The island's settings machine (what the settings screen of the phone edits).
	static UObject* SettingsMachine()
	{
		static auto MachineClass = FindObject<UClass>(L"/Script/FortniteGame.FortMinigameSettingsBuilding");
		UObject* Found = nullptr;

		if (MachineClass)
		{
			auto Machines = UGameplayStatics::GetAllActorsOfClass(GetWorld(), MachineClass);
			Found = Machines.Num() > 0 ? Machines.at(0) : nullptr;
			Machines.Free();
		}

		return Found;
	}

	// Can the player edit this options component, as the game itself says? (the client asks the same question before it opens the screen)
	static void LogEditAccess(UObject* Component, UObject* Pawn)
	{
		if (!Component)
			return;

		auto CanEdit = Component->FindFunction("CanEditOptionsComponent");
		auto EditorV2 = Component->FindFunction("GetPropertyEditorV2Enabled");

		struct { UObject* Pawn; bool ReturnValue; char Pad[7]; } Can{ Pawn, false };
		struct { bool ReturnValue; char Pad[7]; } V2{};

		const bool bCan = CanEdit && Pawn && SafeProcessEvent(Component, CanEdit, &Can);
		const bool bV2 = EditorV2 && SafeProcessEvent(Component, EditorV2, &V2);

		LOG_INFO(LogCreative, "Settings:   {}: CanEditOptionsComponent for {} -> {}, PropertyEditorV2 -> {}", Describe(Component), Describe(Pawn), bCan ? (Can.ReturnValue ? "yes" : "NO") : "unknown",
			bV2 ? (V2.ReturnValue ? "yes" : "no") : "unknown");
	}

	// ------------------------------------------------------------------------------------------------------------------
	// When the game's own handler leaves a device's options unset. The SDK says what an options asset is (a list of options, each with a key and, for the
	// choices, named values), what the component keeps (PlayerOptionData: the overrides, a base object they apply to) and that the actor has a property with
	// the option's name, so this first writes down what is really there, then helps in steps: the base object, the component's own setters again, and last
	// the actor's properties set directly (with the override list updated so the clients get them too).
	// ------------------------------------------------------------------------------------------------------------------
	struct Color4 { float R, G, B, A; };

	struct ValueName
	{
		std::string Display, Name;
	};

	struct OptionDef
	{
		UObject* Object = nullptr;
		std::string Class, Key;
		std::vector<ValueName> Values;
		std::vector<long long> Ints;
		std::vector<Color4> Colors;
		std::vector<float> Floats;
		int DefaultIndex = -1; // the choice an option starts with (the choice options have one at +0xF8)
	};

	static std::string Lower(std::string Text)
	{
		for (auto& Letter : Text)
			Letter = (char)std::tolower((unsigned char)Letter);

		return Text;
	}

	// An FText held in memory as plain text, through the game's own text library.
	static std::string TextAt(const void* Address)
	{
		static auto Function = FindObject<UFunction>(L"/Script/Engine.KismetTextLibrary.Conv_TextToString");
		static auto Library = FindObject<UObject>(L"/Script/Engine.Default__KismetTextLibrary");

		if (!Function || !Library || !Readable(Address, 0x18))
			return std::string();

		struct { char Text[0x18]; char Result[0x10]; } Params{};
		memcpy(Params.Text, Address, 0x18);

		return SafeProcessEvent(Library, Function, &Params) ? TextOf(*(const FString*)Params.Result) : std::string();
	}

	// The options an options asset (PlaylistUserOptions) lists: TArray<PlaylistUserOptionBase*> at +0x38; each has its key at +0x70, the choices a table at +0xE8.
	static std::vector<OptionDef> ReadOptionDefs(UObject* Asset)
	{
		std::vector<OptionDef> Result;

		if (!Asset || !PropDump::IsLiveObject(Asset))
			return Result;

		const char* Base = (const char*)Asset;
		const char* Data = *(const char* const*)(Base + 0x38);
		const int Num = *(const int*)(Base + 0x40);

		if (Num <= 0 || Num > 500 || !Readable(Data, (size_t)Num * 8))
			return Result;

		for (int i = 0; i < Num; ++i)
		{
			auto Object = ((UObject* const*)Data)[i];

			if (!PropDump::IsLiveObject(Object) || !Object->ClassPrivate)
				continue;

			OptionDef Def;
			Def.Object = Object;
			Def.Class = Object->ClassPrivate->GetName();
			const char* O = (const char*)Object;

			if (Readable(O + 0x70, 0x10))
				Def.Key = TextOf(*(const FString*)(O + 0x70));

			size_t Stride = 0;

			if (Def.Class == "PlaylistUserOptionEnum") Stride = 0x28;
			else if (Def.Class == "PlaylistUserOptionIntEnum") Stride = 0x30;
			else if (Def.Class == "PlaylistUserOptionColorEnum") Stride = 0x38;
			else if (Def.Class == "PlaylistUserOptionFloatEnum") Stride = 0x48;

			if (Stride && Readable(O + 0xF8, 4))
				Def.DefaultIndex = *(const int*)(O + 0xF8);

			if (Stride && Readable(O + 0xE8, 0x10))
			{
				const char* Values = *(const char* const*)(O + 0xE8);
				const int Count = *(const int*)(O + 0xF0);

				if (Count > 0 && Count < 200 && Readable(Values, (size_t)Count * Stride))
					for (int j = 0; j < Count; ++j)
					{
						const char* Entry = Values + (size_t)j * Stride;
						Def.Values.push_back({ TextAt(Entry), TextOf(*(const FString*)(Entry + 0x18)) });

						if (Stride == 0x30)
							Def.Ints.push_back(*(const int*)(Entry + 0x28));
						else if (Stride == 0x38)
							Def.Colors.push_back(*(const Color4*)(Entry + 0x28));
						else if (Stride == 0x48)
							Def.Floats.push_back(*(const float*)(Entry + 0x28));
					}
			}

			Result.push_back(Def);
		}

		return Result;
	}

	// What the options asset lists: once for each asset.
	static void DescribeOptionsAsset(UObject* Asset)
	{
		static std::set<UObject*> Done;

		if (!Asset || !Done.insert(Asset).second)
			return;

		const auto Defs = ReadOptionDefs(Asset);
		LOG_INFO(LogCreative, "Options asset {}: {} option(s)", Describe(Asset), (int)Defs.size());

		for (auto& Def : Defs)
		{
			std::string Values;

			for (size_t i = 0; i < Def.Values.size() && i < 14; ++i)
			{
				Values += std::format("{}{}:'{}'/{}", i ? ", " : " ", (int)i, Def.Values[i].Display, Def.Values[i].Name);

				if (i < Def.Ints.size())
					Values += std::format("={}", Def.Ints[i]);
				else if (i < Def.Floats.size())
					Values += std::format("={}", Def.Floats[i]);
				else if (i < Def.Colors.size())
					Values += std::format("=({:.2f} {:.2f} {:.2f} {:.2f})", Def.Colors[i].R, Def.Colors[i].G, Def.Colors[i].B, Def.Colors[i].A);
			}

			LOG_INFO(LogCreative, "Options asset:   {} '{}'{}", Def.Class, Def.Key, Values);
		}
	}

	static const OptionDef* FindDef(const std::vector<OptionDef>& Defs, const std::string& Key)
	{
		for (auto& Def : Defs)
			if (Def.Key == Key)
				return &Def;

		for (auto& Def : Defs)
			if (Lower(Def.Key) == Lower(Key))
				return &Def;

		return nullptr;
	}

	// The index of a named value of a choice option (the request carries the name the screen shows, or the value's own name).
	static int ValueIndex(const OptionDef& Def, const std::string& Wanted)
	{
		const auto W = Lower(Wanted);

		for (size_t i = 0; i < Def.Values.size(); ++i)
			if (Lower(Def.Values[i].Name) == W)
				return (int)i;

		for (size_t i = 0; i < Def.Values.size(); ++i)
			if (Lower(Def.Values[i].Display) == W)
				return (int)i;

		return -1;
	}

	static bool ParseNumber(const std::string& Text, double& Value)
	{
		if (Text.empty())
			return false;

		char* End = nullptr;
		Value = std::strtod(Text.c_str(), &End);
		return End && *End == 0;
	}

	static bool ParseBool(const std::string& Text, bool& Value)
	{
		const auto T = Lower(Text);

		if (T == "true" || T == "on" || T == "yes" || T == "1" || T == "enabled")
		{
			Value = true;
			return true;
		}

		if (T == "false" || T == "off" || T == "no" || T == "0" || T == "disabled")
		{
			Value = false;
			return true;
		}

		return false;
	}

	// What the options component keeps: PlayerOptionData (the override list, the mode, the object they apply to).
	struct DataState
	{
		int Overrides = 0;
		int Mode = 0;
		UObject* Base = nullptr;
		UObject* Mutable = nullptr;
	};

	static char* DataOf(UObject* Component)
	{
		const int Offset = Component ? Component->GetOffset("PlayerOptionData", false) : -1;
		return Offset > 0 && Readable((char*)Component + Offset, 0x50) ? (char*)Component + Offset : nullptr;
	}

	static bool ReadData(UObject* Component, DataState& State)
	{
		const char* Data = DataOf(Component);

		if (!Data)
			return false;

		State.Overrides = *(const int*)(Data + 8);
		State.Mode = *(const unsigned char*)(Data + 0x38);
		State.Base = *(UObject* const*)(Data + 0x40);
		State.Mutable = *(UObject* const*)(Data + 0x48);
		return true;
	}

	// The component and its data, written down: the first time for every kind of component with all its properties.
	static void LogComponent(const char* When, UObject* Component)
	{
		if (!Component)
			return;

		static std::set<std::string> Dumped;
		DataState State;
		static const int AssetOffset = Component->GetOffset("PlayerOptions", false);
		const auto Asset = AssetOffset > 0 ? Component->Get<UObject*>(AssetOffset) : nullptr;

		if (ReadData(Component, State))
		{
			LOG_INFO(LogCreative, "Options component ({}): {} has {} override(s), mode {}, base object {}, mutable object {}, options asset {}", When, Describe(Component), State.Overrides, State.Mode,
				Describe(State.Base), Describe(State.Mutable), Describe(Asset));
		}
		else
		{
			LOG_WARN(LogCreative, "Options component ({}): {} has no readable PlayerOptionData", When, Describe(Component));
		}

		DescribeOptionsAsset(Asset);

		const std::string ClassName = Component->ClassPrivate ? Component->ClassPrivate->GetName() : std::string("?");

		if (Dumped.insert(ClassName).second)
			PropDump::DumpObject("Options component " + ClassName, Component, 2, 120);
	}

	// The object the overrides are applied to is empty when the component was never set up for its actor: it is the actor.
	static bool RepairData(UObject* Component, UObject* Owner)
	{
		DataState State;

		if (!Owner || !ReadData(Component, State) || State.Base)
			return false;

		*(UObject**)(DataOf(Component) + 0x40) = Owner;
		LOG_WARN(LogCreative, "Settings:   the options of {} had no base object, set to the actor", Describe(Component));
		return true;
	}

	// Writes an option into the actor's property of that name. Returns what was done ("" when it could not be).
	static std::string SetActorProperty(UObject* Actor, const OptionDef* Def, const Option& Wanted, std::string& Why)
	{
		auto Property = (const char*)Actor->GetProperty(Wanted.Key, false);

		if (!Property)
		{
			Why = "the actor has no property of that name";
			return std::string();
		}

		const std::string Type = PropDump::TypeOf(Property);
		const int Offset = *(const int*)(Property + Offsets::Offset_Internal);
		const int Size = *(const int*)(Property + PropDump::ElementSizeAt);
		char* Address = (char*)Actor + Offset;

		if (Offset <= 0 || Size <= 0 || !Readable(Address, (size_t)Size))
		{
			Why = "the property is unreadable";
			return std::string();
		}

		const int Index = Def ? ValueIndex(*Def, Wanted.Value) : -1;
		double Number = 0;
		const bool bNumber = ParseNumber(Wanted.Value, Number);

		if (Type == "StrProperty")
		{
			MakeString(Address, Wanted.Value);
			return "text";
		}

		if (Type == "BoolProperty")
		{
			bool bValue = false;

			if (!ParseBool(Wanted.Value, bValue))
			{
				if (Index < 0 && !bNumber)
				{
					Why = "not a yes/no value";
					return std::string();
				}

				bValue = Index >= 0 ? Index != 0 : Number != 0;
			}

			Actor->SetBitfieldValue(Offset, GetFieldMask((void*)Property), bValue);
			return bValue ? "yes" : "no";
		}

		if (Type == "IntProperty")
		{
			long long Value;

			if (Index >= 0)
				Value = Index < (int)Def->Ints.size() ? Def->Ints[Index] : (Index < (int)Def->Floats.size() ? (long long)Def->Floats[Index] : Index);
			else if (bNumber)
				Value = (long long)Number;
			else
			{
				Why = "no such choice";
				return std::string();
			}

			*(int*)Address = (int)Value;
			return std::to_string(Value);
		}

		if (Type == "ByteProperty" || Type == "EnumProperty")
		{
			const void* Enum = *(const void* const*)(Property + (Type == "ByteProperty" ? PropDump::InnerAt : PropDump::SecondAt));
			int Value = -1;

			// the enum's own value of that name (Left, Center, Right ...), else the place in the list
			if (Enum)
				for (int v = 0; v < 64 && Value < 0; ++v)
					if (Lower(PropDump::EnumName(Enum, v)) == Lower(Wanted.Value))
						Value = v;

			if (Value < 0)
				Value = Index >= 0 ? Index : (bNumber ? (int)Number : -1);

			if (Value < 0)
			{
				Why = "no such choice";
				return std::string();
			}

			if (Size == 1) *(unsigned char*)Address = (unsigned char)Value;
			else if (Size == 2) *(short*)Address = (short)Value;
			else if (Size == 4) *(int*)Address = Value;
			else
			{
				Why = "an unexpected enum size";
				return std::string();
			}

			return std::to_string(Value);
		}

		if (Type == "FloatProperty")
		{
			float Value;

			if (Index >= 0)
				Value = Index < (int)Def->Floats.size() ? Def->Floats[Index] : (Index < (int)Def->Ints.size() ? (float)Def->Ints[Index] : (float)Index);
			else if (bNumber)
				Value = (float)Number;
			else
			{
				Why = "no such choice";
				return std::string();
			}

			*(float*)Address = Value;
			return std::to_string(Value);
		}

		if (Type == "StructProperty")
		{
			const void* Struct = *(const void* const*)(Property + PropDump::InnerAt);
			const std::string StructName = PropDump::IsLiveObject(Struct) ? ((UObject*)Struct)->GetName() : std::string();

			if (StructName == "LinearColor" && Def && Index >= 0 && Index < (int)Def->Colors.size() && Size == 16)
			{
				memcpy(Address, &Def->Colors[Index], 16);
				return "a color";
			}

			Why = "a " + StructName + " property that has no value of that name";
			return std::string();
		}

		Why = "a " + Type + " is not handled";
		return std::string();
	}

	// The override list the component replicates: one entry for each option (scope, name, value as text, default), changed in place or added.
	static int MirrorToData(UObject* Component, const std::vector<Option>& Values)
	{
		char* Data = DataOf(Component);

		if (!Data)
			return 0;

		int Done = 0;

		for (auto& V : Values)
		{
			char*& Elements = *(char**)Data;
			int& Num = *(int*)(Data + 8);
			int& Max = *(int*)(Data + 12);

			if (Num < 0 || Num > 5000 || (Num > 0 && !Readable(Elements, (size_t)Num * 0x48)))
				return Done;

			bool bFound = false;

			for (int i = 0; i < Num && !bFound; ++i)
				if (TextOf(*(const FString*)(Elements + (size_t)i * 0x48 + 0x10)) == V.Key)
				{
					MakeString(Elements + (size_t)i * 0x48 + 0x20, V.Value);
					bFound = true;
				}

			if (!bFound)
			{
				Elements = (char*)FMemory::Realloc(Num > 0 ? Elements : nullptr, (size_t)(Num + 1) * 0x48, 0);
				char* Entry = Elements + (size_t)Num * 0x48;
				memset(Entry, 0, 0x48);
				MakeString(Entry + 0x10, V.Key);
				MakeString(Entry + 0x20, V.Value);
				++Num;
				Max = Num;
			}

			++Done;
		}

		return Done;
	}

	// What the game's own setter does after a change: the component's OnAnyPropertyChanged delegate runs the functions bound to it (a billboard draws its new text, the
	// settings machine pushes its values into the minigame). Setting the properties by hand skips that, so the bound functions are run here.
	static int NotifyOptionsChanged(UObject* Component, UObject* Owner)
	{
		const int Offset = Component ? Component->GetOffset("OnAnyPropertyChanged", false) : -1;
		int Ran = 0;
		std::string Names;

		auto Run = [&](UObject* Object, const std::string& Name) {
			auto Function = Object ? Object->FindFunction(Name) : nullptr;

			if (!Function)
				return false;

			char Params[0x100] = {};
			const bool bRan = SafeProcessEvent(Object, Function, Params);
			Names += std::format("{}{} on {}{}", Names.empty() ? "" : ", ", Name, Object->GetName(), bRan ? "" : " (FAILED)");
			return bRan;
		};

		if (Offset > 0 && Readable((char*)Component + Offset, 0x10))
		{
			const char* Delegate = (const char*)Component + Offset;
			const char* Data = *(const char* const*)Delegate;
			const int Num = *(const int*)(Delegate + 8);

			// FMulticastScriptDelegate: a TArray of FScriptDelegate (weak object pointer, function name), 16 bytes each
			if (Num > 0 && Num <= 64 && Readable(Data, (size_t)Num * 16))
				for (int i = 0; i < Num; ++i)
				{
					const char* Entry = Data + (size_t)i * 16;
					UObject* Bound = ((FWeakObjectPtr*)Entry)->Get();

					if (!PropDump::IsLiveObject(Bound))
						Bound = Owner;

					if (Run(Bound, NameAt(Entry + 8)))
						++Ran;
				}
		}

		// nothing was bound that could be found: the update functions the SDK lists for these devices
		if (Ran == 0)
			for (const char* Name : { "OnGameSettingsUpdated", "UpdateGameSettings", "UpdateProperties" })
				if (Run(Owner, Name))
					++Ran;

		LOG_INFO(LogCreative, "Settings:   told {} that its options changed: {}", Describe(Owner), Names.empty() ? std::string("no bound function ran") : Names);
		return Ran;
	}

	// The actor's properties set by hand (and told about it), the override list updated. Returns how many options were set.
	static int ApplyToActor(UObject* Actor, UObject* Component, const std::vector<Option>& Wanted)
	{
		static const int AssetOffset = Component->GetOffset("PlayerOptions", false);
		const auto Defs = ReadOptionDefs(AssetOffset > 0 ? Component->Get<UObject*>(AssetOffset) : nullptr);
		std::vector<Option> Applied;

		for (auto& W : Wanted)
		{
			const OptionDef* Def = FindDef(Defs, W.Key);
			std::string Why;
			const auto What = SetActorProperty(Actor, Def, W, Why);

			if (What.empty())
			{
				LOG_WARN(LogCreative, "Settings:     {} = {}: not set on the actor ({}; {})", W.Key, W.Value, Why, Def ? "the options asset lists it as " + Def->Class : std::string("the options asset does not list it"));
				continue;
			}

			LOG_INFO(LogCreative, "Settings:     {} = {}: set on the actor ({}, option is {})", W.Key, W.Value, What, Def ? Def->Class : std::string("not listed"));
			Applied.push_back(W);

			// a property the actor replicates tells its clients through the on-rep function; run it here too so the server's own copy follows
			if (auto OnRep = Actor->FindFunction("OnRep_" + W.Key))
				SafeProcessEvent(Actor, OnRep, nullptr);
		}

		const int Mirrored = MirrorToData(Component, Applied);

		if (Mirrored > 0)
		{
			DataState State;

			if (ReadData(Component, State) && State.Base)
				if (auto OnRepData = Component->FindFunction("OnRep_PlayerOptionData"))
					SafeProcessEvent(Component, OnRepData, nullptr);

			LOG_INFO(LogCreative, "Settings:     {} override(s) written to the component's list", Mirrored);
		}

		return (int)Applied.size();
	}

	static UObject* MutatorListOf(UObject* Controller);
	static bool ApplyToMutatorList(UObject* List, const std::vector<Option>& Wanted);

	// A device starts with the values its blueprint was made with, which need not be among the values its options offer: the billboard's TextSize starts at 0 and its
	// options are 10, 12, 18 and 24, and the client's options screen cannot show a row whose current value is none of the choices ("[Parameter name]", "Option").
	// In the game the options component sets every option to its default when the device begins; here, every choice option whose property holds a value that is
	// not one of its choices is set to the option's default choice.
	static int ApplyDefaultOptions(UObject* Actor, UObject* Component)
	{
		static const int AssetOffset = Component->GetOffset("PlayerOptions", false);
		const auto Defs = ReadOptionDefs(AssetOffset > 0 ? Component->Get<UObject*>(AssetOffset) : nullptr);
		int Changed = 0;

		for (auto& Def : Defs)
		{
			if (Def.Values.empty() || Def.DefaultIndex < 0 || Def.DefaultIndex >= (int)Def.Values.size())
				continue;

			auto Property = (const char*)Actor->GetProperty(Def.Key, false);

			if (!Property)
				continue;

			const std::string Type = PropDump::TypeOf(Property);
			const int Offset = *(const int*)(Property + Offsets::Offset_Internal);
			const int Size = *(const int*)(Property + PropDump::ElementSizeAt);
			const char* Address = (const char*)Actor + Offset;

			if (Offset <= 0 || Size <= 0 || !Readable(Address, (size_t)Size))
				continue;

			bool bValid = false;
			std::string Current;

			if (Type == "IntProperty")
			{
				const int Value = *(const int*)Address;
				Current = std::to_string(Value);

				if (!Def.Ints.empty())
				{
					for (auto Choice : Def.Ints)
						bValid = bValid || Choice == Value;
				}
				else
					bValid = Value >= 0 && Value < (int)Def.Values.size();
			}
			else if (Type == "FloatProperty")
			{
				const float Value = *(const float*)Address;
				Current = std::to_string(Value);

				for (auto Choice : Def.Floats)
					bValid = bValid || std::fabs(Choice - Value) < 0.001f;

				for (auto Choice : Def.Ints)
					bValid = bValid || std::fabs((float)Choice - Value) < 0.001f;

				if (Def.Floats.empty() && Def.Ints.empty())
					bValid = true; // an enum of names: nothing to compare with
			}
			else if (Type == "ByteProperty" || Type == "EnumProperty")
			{
				const int Value = *(const unsigned char*)Address;
				Current = std::to_string(Value);
				bValid = Value < (int)Def.Values.size();
			}
			else if (Type == "StructProperty" && Size == 16 && !Def.Colors.empty())
			{
				for (auto& Choice : Def.Colors)
					bValid = bValid || memcmp(&Choice, Address, 16) == 0;

				Current = "a color";
			}
			else
				continue;

			if (bValid)
				continue;

			std::string Why;
			const auto What = SetActorProperty(Actor, &Def, Option{ Def.Key, Def.Values[Def.DefaultIndex].Name }, Why);

			if (What.empty())
			{
				LOG_WARN(LogCreative, "Device defaults: {} holds {} on {}, not one of its choices, and the default '{}' could not be set ({})", Def.Key, Current, Describe(Actor), Def.Values[Def.DefaultIndex].Name, Why);
				continue;
			}

			++Changed;
			LOG_INFO(LogCreative, "Device defaults: {} held {} on {}, not one of its choices; now '{}' ({})", Def.Key, Current, Describe(Actor), Def.Values[Def.DefaultIndex].Name, What);

			if (auto OnRep = Actor->FindFunction("OnRep_" + Def.Key))
				SafeProcessEvent(Actor, OnRep, nullptr);
		}

		if (Changed > 0)
			((AActor*)Actor)->ForceNetUpdate();

		return Changed;
	}

		// What the move tool holds around a copy (duplicate) or cut request: the actors it was asked for, what it is already spawning (the duplicates it makes, the record spawner), and
	// what its own checks say (it may spawn at all; the island volume it works in, with and without the permission check). The 7 Nov log shows the request arriving 17 times in
	// 3 seconds and the server doing nothing visible afterwards, so these write down why.
	static void LogMoveToolState(UObject* Tool, const char* When, const char* Request, int Count)
	{
		if (!Tool)
			return;

		auto ArrayCount = [&](const char* Name) {
			const int Offset = Tool->GetOffset(Name, false);
			return Offset > 0 && Readable((char*)Tool + Offset, 16) ? *(int*)((char*)Tool + Offset + 8) : -1;
		};

		const int SpawnerOffset = Tool->GetOffset("ActiveRecordSpawner", false);
		UObject* Spawner = SpawnerOffset > 0 ? Tool->Get<UObject*>(SpawnerOffset) : nullptr;

		bool bCan = false, bCanRan = false;

		if (auto Function = Tool->FindFunction("CanUtilizeMoveToolSpawningFunctionality"))
		{
			struct { bool ReturnValue; char Pad[7]; } Params{};
			bCanRan = SafeProcessEvent(Tool, Function, &Params);
			bCan = Params.ReturnValue;
		}

		std::string Volumes;

		for (bool bPermissions : { false, true })
			if (auto Function = Tool->FindFunction("GetCurrentVolume"))
			{
				struct { bool bMustHavePermissions; char Pad[7]; UObject* ReturnValue; } Params{ bPermissions, {}, nullptr };
				const bool bRan = SafeProcessEvent(Tool, Function, &Params);
				Volumes += std::format("{}volume{} {}", Volumes.empty() ? "" : ", ", bPermissions ? " (with the permission check)" : "", bRan ? Describe(Params.ReturnValue) : std::string("(call failed)"));
			}

		LOG_INFO(LogCreative, "Move tool ({}) {}: {} actor(s) asked for; the tool holds {} to duplicate, {} spawned by the helper, {} newly placed, {} selected, record spawner {}; can spawn: {}; {}", When, Request, Count, ArrayCount("ToDuplicateActors"),
			ArrayCount("SpawnHelperNewlyPlacedActors"), ArrayCount("NewlyPlacedActors"), ArrayCount("SelectedActors"), Describe(Spawner), bCanRan ? (bCan ? "yes" : "NO") : "unknown", Volumes);
	}

	// The change of an actor's options, before and after the game's own handler.

	static void HandleActorOptions(UObject* Controller, void* Params, bool bAfter)
	{
		struct Request { UObject* Target; TArray<FString> Keys; TArray<FString> Values; };
		auto& R = *(Request*)Params;
		const auto Wanted = ReadOptions(R.Keys, R.Values);
		auto Component = OptionsComponentOf(R.Target);

		if (!bAfter)
		{
			LOG_INFO(LogCreative, "Settings: {} asks to change {} option(s) of {} ({}): {}", Describe(Controller), (int)Wanted.size(), Describe(R.Target), Component ? "has an options component" : "NO options component",
				Summary(Wanted));

			std::vector<Option> Before;

			if (Component && CurrentOptions(Component, Before))
				LOG_INFO(LogCreative, "Settings:   before: {} option(s) set: {}", (int)Before.size(), Summary(Before));

			LogComponent("before the game's handler", Component);
			LogEditAccess(Component, ControllerPawn(Controller));
			return;
		}

		if (!Component || Wanted.empty())
			return;

		std::vector<Option> Now;

		if (!CurrentOptions(Component, Now))
		{
			LOG_WARN(LogCreative, "Settings:   the options of {} can not be read (no GetPropertyOverrides)", Describe(R.Target));
			return;
		}

		int Set = Matching(Wanted, Now);
		LOG_INFO(LogCreative, "Settings:   after the game's handler: {} of {} requested option(s) are set ({} set in all): {}", Set, (int)Wanted.size(), (int)Now.size(), Summary(Now));

		if (Set >= (int)Wanted.size())
			return;

		auto MissingOf = [&]() {
			std::vector<Option> Missing;

			for (auto& W : Wanted)
				if (Matching({ W }, Now) == 0)
					Missing.push_back(W);

			return Missing;
		};

		auto Missing = MissingOf();
		LOG_WARN(LogCreative, "Settings:   {} option(s) were not applied, setting them: {}", (int)Missing.size(), Summary(Missing));
		LogComponent("after the game's handler", Component);

		// 1. the component's own setters (after giving it the object it applies the options to, when it has none)
		const bool bRepaired = RepairData(Component, R.Target);
		ApplyOptions(Component, Missing);

		if (CurrentOptions(Component, Now) && !MissingOf().empty())
		{
			// 2. the overrides the component still holds back
			if (auto Resolve = Component->FindFunction("ResolvePendingRepOverrides"))
			{
				SafeProcessEvent(Component, Resolve, nullptr);
				CurrentOptions(Component, Now);
			}
		}

		Missing = MissingOf();

		// 3. the actor's properties directly
		if (!Missing.empty())
		{
			LOG_WARN(LogCreative, "Settings:   the component did not take {} option(s){}, setting the actor's properties directly", (int)Missing.size(), bRepaired ? " (even with a base object)" : "");
			const int Direct = ApplyToActor(R.Target, Component, Missing);
			LOG_INFO(LogCreative, "Settings:   {} of {} option(s) set on the actor", Direct, (int)Missing.size());

			if (Direct > 0)
				NotifyOptionsChanged(Component, R.Target);

			// options named "Class:Property" are the mutators' (the voice chat channel ...), not the machine's: they go to the island's mutator list
			std::vector<Option> Scoped;

			for (auto& M : Missing)
				if (M.Key.find(':') != std::string::npos)
					Scoped.push_back(M);

			if (!Scoped.empty())
				if (auto List = MutatorListOf(Controller))
					ApplyToMutatorList(List, Scoped);
		}

		if (CurrentOptions(Component, Now))
		{
			Set = Matching(Wanted, Now);
			LOG_INFO(LogCreative, "Settings:   after our own change: {} of {} requested option(s) are in the component's list: {}", Set, (int)Wanted.size(), Summary(Now));
		}

		if (auto Actor = (AActor*)R.Target)
			Actor->ForceNetUpdate();
	}

	// The island's mutator list (the volume's FortMutatorListComponent, "CreativeIslandMutatorList"): what the settings tab's gameplay options (time of day, light, fog,
	// starting health, shields ...) are stored in. Their keys are "ClassOfTheSetting:Property", and the mutators listen to the list.
	static UObject* MutatorListOf(UObject* Controller)
	{
		static auto ListClass = FindObject<UClass>(L"/Script/FortniteGame.FortMutatorListComponent");

		if (!ListClass)
			return nullptr;

		AActor* Volume = nullptr;

		if (auto Machine = SettingsMachine())
		{
			const int Offset = Machine->GetOffset("SettingsVolume", false);

			if (Offset != -1)
				Volume = Machine->Get<AActor*>(Offset);
		}

		if (!Volume && Controller)
			Volume = ((AFortPlayerControllerAthena*)Controller)->GetCreativePlotLinkedVolume();

		return Volume && PropDump::IsLiveObject(Volume) ? (UObject*)Volume->GetComponentByClass(ListClass) : nullptr;
	}

	static bool ListOverrides(UObject* List, std::vector<Option>& Out)
	{
		auto Function = List ? List->FindFunction("GetPropertyOverridesAsStrings") : nullptr;

		if (!Function)
			return false;

		struct { char Map[0x50]; } Params{};

		if (!SafeProcessEvent(List, Function, &Params))
			return false;

		Out = ReadStringMap(Params.Map);
		return true;
	}

	// Sets options in the mutator list with its own function (merging with what it holds, or replacing it), and says what it holds afterwards.
	static bool ApplyToMutatorList(UObject* List, const std::vector<Option>& Wanted)
	{
		std::vector<Option> Now;
		ListOverrides(List, Now);

		bool bHeld = false;

		for (const char* Name : { "AddOrUpdatePropertyOverrides", "SetPropertyOverrides" })
		{
			auto Function = List->FindFunction(Name);

			if (!Function)
				continue;

			std::vector<Option> Send = Wanted;

			// SetPropertyOverrides replaces the whole list: what it holds stays
			if (std::string(Name) == "SetPropertyOverrides")
				for (auto& Held : Now)
				{
					bool bReplaced = false;

					for (auto& W : Wanted)
						bReplaced = bReplaced || W.Key == Held.Key;

					if (!bReplaced)
						Send.push_back(Held);
				}

			struct { char Map[0x50]; } Params{};
			BuildStringMap(Params.Map, Send);
			const bool bRan = SafeProcessEvent(List, Function, &Params);
			std::vector<Option> After;
			ListOverrides(List, After);
			LOG_INFO(LogCreative, "Settings:   {} on the mutator list {} and now holds {} option(s), {} of the {} wanted among them", Name, bRan ? "ran" : "FAILED (exception)", (int)After.size(), Matching(Wanted, After), (int)Wanted.size());

			if (Matching(Wanted, After) >= (int)Wanted.size())
			{
				bHeld = true;
				break;
			}
		}

		if (auto Resolve = List->FindFunction("ResolvePendingRepOverrides"))
			SafeProcessEvent(List, Resolve, nullptr);

		if (auto OnRep = List->FindFunction("OnRep_PropertyOverrideArray"))
			SafeProcessEvent(List, OnRep, nullptr);

		return bHeld;
	}

	// The island's game settings (time of day, light, fog, starting health ...): the island's mutator list takes them.
	static void HandleGameplayOptions(UObject* Controller, void* Params, bool bAfter)
	{
		struct Request { TArray<FString> Keys; TArray<FString> Values; };
		auto& R = *(Request*)Params;
		const auto Wanted = ReadOptions(R.Keys, R.Values);
		auto List = MutatorListOf(Controller);

		if (!bAfter)
		{
			std::vector<Option> Before;
			const bool bRead = ListOverrides(List, Before);
			LOG_INFO(LogCreative, "Settings: {} asks to change {} game option(s); the island's mutator list is {} ({} option(s) held before): {}", Describe(Controller), (int)Wanted.size(), List ? Describe(List) : std::string("NOT FOUND"),
				bRead ? (int)Before.size() : -1, Summary(Wanted, 30));

			static bool bDumped = false;

			if (List && !bDumped)
			{
				bDumped = true;
				PropDump::DumpObject("Mutator list", List, 1, 70);
			}

			return;
		}

		if (!List || Wanted.empty())
			return;

		std::vector<Option> Now;

		if (!ListOverrides(List, Now))
		{
			LOG_WARN(LogCreative, "Settings:   the mutator list {} can not be read (no GetPropertyOverridesAsStrings)", Describe(List));
			return;
		}

		const int Set = Matching(Wanted, Now);
		LOG_INFO(LogCreative, "Settings:   after the game's handler: {} of {} requested game option(s) are in the mutator list ({} held in all): {}", Set, (int)Wanted.size(), (int)Now.size(), Summary(Now, 30));

		if (Set >= (int)Wanted.size())
			return;

		std::vector<Option> Missing;

		for (auto& W : Wanted)
			if (Matching({ W }, Now) == 0)
				Missing.push_back(W);

		LOG_WARN(LogCreative, "Settings:   {} game option(s) are not in the mutator list, putting them there: {}", (int)Missing.size(), Summary(Missing, 30));
		ApplyToMutatorList(List, Missing);

		if (auto Actor = (AActor*)((AFortPlayerControllerAthena*)Controller)->GetCreativePlotLinkedVolume())
			Actor->ForceNetUpdate();
	}

	void Before(UObject* Context, UFunction* Function, void* Params, int Kind)
	{
		if (!Params)
			return;

		switch (Kind)
		{
		case ActorOptions:
			HandleActorOptions(Context, Params, false);
			break;
		case GameplayOptions:
			HandleGameplayOptions(Context, Params, false);
			break;
		case ItemListOptions:
		{
			struct Request { UObject* Container; int Index; int Pad; TArray<FString> Keys; TArray<FString> Values; };
			auto& R = *(Request*)Params;
			LOG_INFO(LogCreative, "Settings: {} asks to change an item list entry {} of {}: {}", Describe(Context), R.Index, Describe(R.Container), Summary(ReadOptions(R.Keys, R.Values)));
			break;
		}
		case MessageComponents:
		{
			struct Request { TArray<UObject*> Components; TArray<char> Channels; };
			auto& R = *(Request*)Params;
			std::string Names;

			for (int i = 0; i < R.Components.Num() && i < 8; ++i)
				Names += (i ? ", " : "") + Describe(R.Components.at(i));

			LOG_INFO(LogCreative, "Settings: {} asks to change {} message component(s): {}", Describe(Context), R.Components.Num(), Names);
			break;
		}
		case GameDescription:
		{
			struct Request { UObject* Target; };
			LOG_INFO(LogCreative, "Settings: {} asks to change the game name and description of {}", Describe(Context), Describe(((Request*)Params)->Target));
			break;
		}
					case MoveDuplicate:
			case MoveCut:
			case MoveSpawn:
			{
				static int Logged[KindCount] = {};

				if (++Logged[Kind] > 40)
					break;

				const char* Request = Kind == MoveDuplicate ? "ServerDuplicateStartInteracting" : (Kind == MoveCut ? "ServerHandshakeFromClientForReceivingCutActors" : "ServerSpawnSelectedActorsWithTransform");
				int Count = -1;
				std::string Names;

				if (Kind == MoveDuplicate)
				{
					struct Request_ { TArray<UObject*> Actors; };
					auto& R = *(Request_*)Params;
					Count = R.Actors.Num();

					for (int i = 0; i < R.Actors.Num() && i < 6; ++i)
						Names += (i ? ", " : "") + Describe(R.Actors.at(i));

					LOG_INFO(LogCreative, "Move tool: {} asks to duplicate: {}", Describe(Context), Names.empty() ? std::string("(no actors)") : Names);
				}

				LogMoveToolState(Context, "before", Request, Count);
				break;
			}
			case DeviceButton:
			{
			struct Request { UObject* Component; };
			LOG_INFO(LogCreative, "Settings: {} pressed a device button: {}", Describe(Context), Describe(((Request*)Params)->Component));
			break;
		}
		case Interact:
		{
			struct Request { UObject* Target; UObject* Component; uint8_t Type; char Pad[7]; UObject* Data; uint8_t Attempt; char Pad2[3]; int RequestId; };
			auto& R = *(Request*)Params;

			// players interact all the time; only the first few hundred requests are written down
			static int Logged = 0;

			if (++Logged > 400)
				break;

			auto Options = OptionsComponentOf(R.Target);

			LOG_INFO(LogCreative, "Settings: interact request {}: type {}, attempt {}, on {} ({}), component {}", R.RequestId, (int)R.Type, (int)R.Attempt, Describe(R.Target), Options ? "it has an options component" : "no options component",
				Describe(R.Component));

			if (Options)
			{
				auto Pawn = ControllerPawn(FirstController());
				LogEditAccess(Options, Pawn);
			}

			break;
		}
		}
	}

	void After(UObject* Context, UFunction* Function, void* Params, int Kind)
	{
		if (!Params)
			return;

				if (Kind == ActorOptions)
			HandleActorOptions(Context, Params, true);
		else if (Kind == GameplayOptions)
			HandleGameplayOptions(Context, Params, true);
		else if (Kind == MoveDuplicate || Kind == MoveCut || Kind == MoveSpawn)
		{
			static int Logged = 0;

			if (++Logged <= 40)
				LogMoveToolState(Context, "after", Kind == MoveDuplicate ? "ServerDuplicateStartInteracting" : (Kind == MoveCut ? "ServerHandshakeFromClientForReceivingCutActors" : "ServerSpawnSelectedActorsWithTransform"), -1);
		}
	}

	// What a device holds after it was placed: its options component (the asset that lists its options, what is set, whether the player may edit it) and,
	// once for each kind of device, every property of the actor.
	void DescribeDevice(UObject* Device, UObject* BuilderController)
	{
		if (!Device)
			return;

		static std::set<std::string> Dumped;
		auto Actor = (AActor*)Device;
		auto Component = OptionsComponentOf(Device);

		static auto ReplicatesOffset = Device->GetOffset("bReplicates", false);
		static auto ReplicatesMask = ReplicatesOffset == -1 ? (uint8_t)0 : GetFieldMask(Device->GetProperty("bReplicates"));

		LOG_INFO(LogCreative, "Device check: {} at {}, replicates {}, {}", Describe(Device), Where(Actor),
			ReplicatesOffset != -1 && ReplicatesMask ? (int)Device->ReadBitfieldValue(ReplicatesOffset, ReplicatesMask) : -1, Component ? "has an options component" : "has NO options component");

		if (Component)
		{
			static auto OptionsAssetOffset = Component->GetOffset("PlayerOptions", false);
			std::vector<Option> Set;
			CurrentOptions(Component, Set);

			LOG_INFO(LogCreative, "Device check:   options asset {}, {} option(s) set: {}", Describe(OptionsAssetOffset == -1 ? nullptr : Component->Get<UObject*>(OptionsAssetOffset)), (int)Set.size(), Summary(Set, 8));
			LogComponent("when placed", Component);
			ApplyDefaultOptions(Device, Component);
			LogEditAccess(Component, ControllerPawn(BuilderController));
		}

		const std::string ClassName = Device->ClassPrivate ? Device->ClassPrivate->GetName() : std::string("?");

		if (Dumped.insert(ClassName).second)
			PropDump::DumpObject("Device dump " + ClassName, Device, 1, 100);
	}
}

// Returns how many actors were spawned for the device (0 when none could be).
static int BuildOwnDevice(const FVector& Base, UObject* Playset)
{
	const std::string Name = Playset->GetName();

	// the device playsets are PID_CP_Devices_<Name>, the later ones PID_019_PC_Device_<Name>
	std::string Device;

	for (const char* Prefix : { "PID_CP_Devices_", "PID_019_PC_Device_", "PID_CP_Device_" })
		if (Name.rfind(Prefix, 0) == 0)
		{
			Device = Name.substr(std::strlen(Prefix));
			break;
		}

	if (Device.empty())
		return 0;

	auto MapActors = ReadDeviceMap(Device);

	UClass* Class = nullptr;

	if (MapActors.empty())
	{
		Class = FindDeviceClass(Device);

		if (!Class)
		{
			LOG_WARN(LogCreative, "Own device {}: neither the map nor the device folder gave an actor class", Device);
			return 0;
		}
	}

	// the thrower, for the team and the build animation
	static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
	AFortPlayerControllerAthena* Builder = nullptr;
	{
		auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

		for (int i = 0; i < Controllers.Num() && !Builder; ++i)
		{
			auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);
			const int PlaysetOffset = Controller ? Controller->GetOffset("CurrentPlayset", false) : -1;

			if (Controller && PlaysetOffset != -1 && Controller->Get<UObject*>(PlaysetOffset) == Playset)
				Builder = Controller;
		}

		if (!Builder && Controllers.Num() > 0)
			Builder = (AFortPlayerControllerAthena*)Controllers.at(0);

		Controllers.Free();
	}

	int Team = -1;

	if (Builder)
		if (auto State = Cast<AFortPlayerStateAthena, false>(Builder->GetPlayerState()))
			Team = State->GetTeamIndex();

	// the playset's own offset for its content (the devices sit 75 above the middle of their grid cell)
	double OffsetZ = 0;
	{
		const int UseOffset = Playset->GetOffset("bUseLocationOffset", false);
		const int LocationOffset = Playset->GetOffset("LocationOffset", false);

		if (UseOffset != -1 && LocationOffset != -1 && Playset->Get<bool>(UseOffset) && Readable((char*)Playset + LocationOffset, 12))
			OffsetZ = ((const float*)((char*)Playset + LocationOffset))[2];
	}

		static auto BuildingActorClass = FindObject<UClass>(L"/Script/FortniteGame.BuildingActor");
	int Spawned = 0;

	if (MapActors.empty())
	{
		MapActor Single;
		Single.Class = Class;
		MapActors.push_back(Single);
	}

	for (auto& Entry : MapActors)
	{
		FRotator Rotation{};
		Rotation.Pitch = Entry.Pitch; Rotation.Yaw = Entry.Yaw; Rotation.Roll = Entry.Roll;

		FTransform Transform{};
		Transform.Translation = FVector((float)((double)Base.X + Entry.X), (float)((double)Base.Y + Entry.Y), (float)((double)Base.Z + OffsetZ + Entry.Z));
		Transform.Rotation = Rotation.Quaternion();
		Transform.Scale3D = FVector((float)Entry.ScaleX, (float)Entry.ScaleY, (float)Entry.ScaleZ);

		auto Spawned_ = GetWorld()->SpawnActor<AActor>(Entry.Class, Transform);

		if (!Spawned_)
		{
			LOG_WARN(LogCreative, "Own device {}: spawning {} failed", Device, Entry.Class->GetName());
			continue;
		}

		++Spawned;

		// an actor that does not replicate is never seen by a client
		{
			static auto ReplicatesOffset = Spawned_->GetOffset("bReplicates", false);
			static auto ReplicatesMask = ReplicatesOffset == -1 ? (uint8_t)0 : GetFieldMask(Spawned_->GetProperty("bReplicates"));
			static auto SetReplicatesFn = FindObject<UFunction>(L"/Script/Engine.Actor.SetReplicates");

			const bool bReplicates = ReplicatesOffset != -1 && ReplicatesMask && Spawned_->ReadBitfieldValue(ReplicatesOffset, ReplicatesMask);

			if (!bReplicates && SetReplicatesFn)
			{
				bool bNew = true;
				Spawned_->ProcessEvent(SetReplicatesFn, &bNew);
				LOG_INFO(LogCreative, "Own device {}: {} did not replicate, it does now", Device, Entry.Class->GetName());
			}

			Spawned_->FlushNetDormancy();
			Spawned_->ForceNetUpdate();
		}

		// a building actor is told who built it (team, build animation)
		if (BuildingActorClass && InheritsFrom(Entry.Class, BuildingActorClass))
		{
			if (Builder)
				((ABuildingActor*)Spawned_)->InitializeBuildingActor(Builder, (ABuildingActor*)Spawned_, true);

			if (Team >= 0)
				((ABuildingActor*)Spawned_)->SetTeam((unsigned char)Team);
		}

		LOG_INFO(LogCreative, "Own device {}: spawned {} ({}) at {}", Device, Entry.Class->GetName(), Describe(Spawned_), Where(Spawned_));
		CreativeSettings::DescribeDevice(Spawned_, Builder);
	}

	LOG_INFO(LogCreative, "Own device {}: {} actors spawned (builder {}, team {})", Device, Spawned, Describe(Builder), Team);
	return Spawned;
}

// ---------------------------------------------------------------------------------------------------------------------
// A survey of every prefab: the pieces each lists, measured, written to the log as "Architect input" lines (what tools/PrefabPreview reads) and what the
// layout makes of them in one line. One playset every tick of the driver (a quarter second) so the game keeps running.
// ---------------------------------------------------------------------------------------------------------------------
static std::vector<std::string> SurveyQueue;
static size_t SurveyNext = 0;
static bool bSurveyActive = false;

void CreativeIsland::StartPrefabSurvey()
{
	SurveyQueue.clear();
	SurveyNext = 0;

	for (auto& Asset : RegistryAssets("/Game/Playsets", false))
		if (Asset.second == "FortPlaysetItemDefinition")
			SurveyQueue.push_back(Asset.first);

	std::sort(SurveyQueue.begin(), SurveyQueue.end());
	bSurveyActive = !SurveyQueue.empty();
	LOG_INFO(LogCreative, "Prefab survey: {} playsets to describe, one every quarter second", (int)SurveyQueue.size());
}

void CreativeIsland::StepPrefabSurvey()
{
	if (!bSurveyActive)
		return;

	if (SurveyNext >= SurveyQueue.size())
	{
		bSurveyActive = false;
		LOG_INFO(LogCreative, "Prefab survey: finished ({} playsets)", (int)SurveyQueue.size());
		return;
	}

	const std::string Package = SurveyQueue[SurveyNext++];
	const std::string Name = Package.substr(Package.find_last_of('/') + 1);
	const std::string ObjectPath = Package + "." + Name;
	const std::wstring Path(ObjectPath.begin(), ObjectPath.end());
	static auto PlaysetClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetItemDefinition");
	UObject* Playset = FindObject<UObject>(Path.c_str());

	if (!Playset)
		Playset = SafeLoadAsset(PlaysetClass, Path.c_str());

	if (!Playset)
	{
		LOG_WARN(LogCreative, "Prefab survey {}/{}: {} could not be loaded", (int)SurveyNext, (int)SurveyQueue.size(), Name);
		return;
	}

	std::vector<PieceInfo> Pieces;
	int Failed = 0, Skipped = 0;
	LoadPieces(Playset, FVector(0.f, 0.f, 30000.f), Pieces, Failed, Skipped);

	const auto Request = MakeRequest(Playset, Pieces);
	LogRequest(Request);
	const auto Plan = Architect::Plan(Request);

	LOG_INFO(LogCreative, "Prefab survey {}/{}: {} is {} x {} x {}, {} piece classes ({} could not be loaded, {} are not pieces); the layout places {} pieces ({}, core {} x {}, {} levels)", (int)SurveyNext,
		(int)SurveyQueue.size(), Name, Request.SizeX, Request.SizeY, Request.SizeZ, (int)Pieces.size(), Failed, Skipped, (int)Plan.Items.size(), Plan.bExhibit ? "an exhibit" : "a building", Plan.CoreX,
		Plan.CoreY, Plan.Levels);
}

// Test: every device playset in a grid next to the first player (8 to a row, 900 apart, on the ground).
void CreativeIsland::SpawnAllDevices()
{
	static auto PlaysetClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetItemDefinition");
	std::vector<std::string> Packages;

	for (auto& Asset : RegistryAssets("/Game/Playsets", false))
	{
		if (Asset.second != "FortPlaysetItemDefinition")
			continue;

		const auto Short = Asset.first.substr(Asset.first.find_last_of('/') + 1);

		if (Short.rfind("PID_CP_Devices_", 0) == 0 || Short.rfind("PID_019_PC_Device_", 0) == 0 || Short.rfind("PID_CP_Device_", 0) == 0)
			Packages.push_back(Asset.first);
	}

	std::sort(Packages.begin(), Packages.end());

	auto Pawn = (AActor*)CreativeSettings::ControllerPawn(CreativeSettings::FirstController());

	if (!Pawn)
	{
		LOG_WARN(LogCreative, "Place every device: there is no player pawn to place them next to");
		return;
	}

	const auto Where_ = Pawn->GetActorLocation();
	const FVector Start((float)Where_.X + 1200.f, (float)Where_.Y - 3000.f, (float)Where_.Z - 88.f);
	LOG_INFO(LogCreative, "Place every device: {} device playsets, starting at {:.0f} {:.0f} {:.0f}", (int)Packages.size(), (double)Start.X, (double)Start.Y, (double)Start.Z);

	int Index = 0, Actors = 0, Failed = 0;

	for (auto& Package : Packages)
	{
		const std::string Name = Package.substr(Package.find_last_of('/') + 1);
		const std::string ObjectPath = Package + "." + Name;
		const std::wstring Path(ObjectPath.begin(), ObjectPath.end());
		UObject* Playset = FindObject<UObject>(Path.c_str());

		if (!Playset)
			Playset = SafeLoadAsset(PlaysetClass, Path.c_str());

		if (!Playset)
		{
			LOG_WARN(LogCreative, "Place every device: {} could not be loaded", Name);
			++Failed;
			continue;
		}

		const FVector At(Start.X + (Index % 8) * 900.f, Start.Y + (Index / 8) * 900.f, Start.Z);
		LOG_INFO(LogCreative, "Place every device: {} ({}) at {:.0f} {:.0f}", Name, Index, (double)At.X, (double)At.Y);
		const int Placed = BuildOwnDevice(At, Playset);

		if (Placed <= 0)
			++Failed;

		Actors += Placed;
		++Index;
	}

	LOG_INFO(LogCreative, "Place every device: {} playsets, {} actors placed, {} playsets gave nothing", Index, Actors, Failed);
}

void CreativeIsland::LoadThrownPrefab(UObject* Projectile, UObject* LoadedAsset)
{
	// The grenade's OnLoaded callback fires for every asset it loads (sounds, effects, ...), so only a real playset is used. Anything else is ignored.
	static auto PlaysetClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlaysetItemDefinition");

	auto IsPlayset = [&](UObject* Object) { return Object && PlaysetClass && Object->IsA(PlaysetClass); };

	UFortPlaysetItemDefinition* Playset = IsPlayset(LoadedAsset) ? (UFortPlaysetItemDefinition*)LoadedAsset : nullptr;

	if (!Playset && Projectile)
	{
		static auto CachedPlaysetOffset = Projectile->GetOffset("CachedPlayset", false);

		if (CachedPlaysetOffset != -1)
		{
			auto Cached = Projectile->Get<UObject*>(CachedPlaysetOffset);

			if (IsPlayset(Cached))
				Playset = (UFortPlaysetItemDefinition*)Cached;
		}
	}

	if (!Globals::bLoadThrownPrefabs)
	{
		LOG_INFO(LogCreative, "Thrown prefab: projectile {} loaded {} (playset: {}); loading thrown prefabs is off, so nothing is streamed in", Describe(Projectile), Describe(LoadedAsset), Describe(Playset));

		// What the game itself put in the volume it spawned, to see which part is missing.
		static auto OffVolumeClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolume");
		static auto OffStreamClass = FindObject<UClass>("/Script/FortniteGame.PlaysetLevelStreamComponent");

		if (OffVolumeClass)
		{
			auto Volumes = UGameplayStatics::GetAllActorsOfClass(GetWorld(), OffVolumeClass);

			for (int i = 0; i < Volumes.Num(); ++i)
			{
				auto Volume = (AFortVolume*)Volumes.at(i);

				if (!Volume || Volume->GetName().find("PrefabGrenades") == std::string::npos)
					continue;

				static auto VolumePlaysetOffset = Volume->GetOffset("CurrentPlayset", false);
				auto Stream = OffStreamClass ? (UObject*)Volume->GetComponentByClass(OffStreamClass) : nullptr;
				static auto StreamPlaysetOffset = Stream ? Stream->GetOffset("CurrentPlayset", false) : -1;

				LOG_INFO(LogCreative, "  prefab volume {} at {}: state {}, volume playset {}, stream component {}, stream playset {}", Volume->GetName(), Where(Volume), (int)Volume->GetVolumeState(),
					Describe(VolumePlaysetOffset == -1 ? nullptr : Volume->Get<UObject*>(VolumePlaysetOffset)), Describe(Stream), Describe(Stream && StreamPlaysetOffset != -1 ? Stream->Get<UObject*>(StreamPlaysetOffset) : nullptr));
			}

			Volumes.Free();
		}

		return;
	}

	LOG_INFO(LogCreative, "Thrown prefab: projectile {} loaded {} ({}), playset to use: {}", Describe(Projectile), Describe(LoadedAsset), LoadedAsset && !IsPlayset(LoadedAsset) ? "not a playset, ignored" : "playset", Describe(Playset));

	static auto VolumeClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolume");

	if (!VolumeClass)
		return;

	static std::set<std::string> AlreadyLoaded; // by name, an address can be reused by a later volume

	auto Volumes = UGameplayStatics::GetAllActorsOfClass(GetWorld(), VolumeClass);
	int Loaded = 0;

	for (int i = 0; i < Volumes.Num(); ++i)
	{
		auto Volume = (AFortVolume*)Volumes.at(i);

		if (!Volume)
			continue;

		const auto Name = Volume->GetName();

		if (Name.find("PrefabGrenades") == std::string::npos || AlreadyLoaded.contains(Name))
			continue;

		// The playset: the one the callback gave, else the grenade's cached one, else what the volume or its stream component already holds,
		// else what the thrower picked (ServerSetPlayset sets the controller's CurrentPlayset when a prefab is picked or thrown).
		UFortPlaysetItemDefinition* UsePlayset = Playset;
		const char* Source = "the grenade";

		auto Try = [&](UObject* Candidate, const char* From) {
			if (!UsePlayset && IsPlayset(Candidate))
			{
				UsePlayset = (UFortPlaysetItemDefinition*)Candidate;
				Source = From;
			}
		};

		static auto VolumePlaysetOffset = Volume->GetOffset("CurrentPlayset", false);

		if (VolumePlaysetOffset != -1)
			Try(Volume->Get<UObject*>(VolumePlaysetOffset), "the volume");

		static auto StreamClass = FindObject<UClass>("/Script/FortniteGame.PlaysetLevelStreamComponent");

		if (auto Stream = StreamClass ? (UObject*)Volume->GetComponentByClass(StreamClass) : nullptr)
		{
			static auto StreamPlaysetOffset = Stream->GetOffset("CurrentPlayset", false);

			if (StreamPlaysetOffset != -1)
				Try(Stream->Get<UObject*>(StreamPlaysetOffset), "the volume's stream component");
		}

		if (!UsePlayset)
		{
			static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
			auto Controllers = ControllerClass ? UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass) : TArray<AActor*>();

			for (int c = 0; c < Controllers.Num() && !UsePlayset; ++c)
			{
				auto Controller = Controllers.at(c);
				static auto ControllerPlaysetOffset = Controller ? Controller->GetOffset("CurrentPlayset", false) : -1;

				if (Controller && ControllerPlaysetOffset != -1)
					Try(Controller->Get<UObject*>(ControllerPlaysetOffset), "the thrower's selection");
			}

			if (ControllerClass)
				Controllers.Free();
		}

		if (!UsePlayset)
		{
			LOG_WARN(LogCreative, "Thrown prefab: no playset known for {} yet, waiting for the next callback", Name);
			continue;
		}

		AlreadyLoaded.insert(Name);
		static auto PrefabStreamClass = FindObject<UClass>("/Script/FortniteGame.PlaysetLevelStreamComponent");
		auto HasStream = [&]() -> bool { return PrefabStreamClass && Volume->GetComponentByClass(PrefabStreamClass) != nullptr; };

		// What this playset is made of, so a prefab that does not work can be told apart from one that has nothing to place.
		{
			static auto WorldOffset = UsePlayset->GetOffset("PlaysetToSpawn", false);
			static auto SaveRecordOffset = UsePlayset->GetOffset("LevelSaveRecord", false);
			static auto CookedOffset = UsePlayset->GetOffset("bHasBeenCooked", false);
			static auto SizeXOffset = UsePlayset->GetOffset("SizeX", false);
			static auto PropsOffset = UsePlayset->GetOffset("AssociatedPlaysetProps", false);

			std::string LevelPath = "n/a";

			if (WorldOffset != -1)
			{
				auto& Soft = UsePlayset->Get<TSoftObjectPtr<UObject>>(WorldOffset);
				LevelPath = Soft.IsValid() ? Soft.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("not set");
			}

			LOG_INFO(LogCreative, "Thrown prefab: playset {} streams level {}, level save record {}, cooked {}, size {} x {} x {}, {} associated props", UsePlayset->GetName(), LevelPath,
				Describe(SaveRecordOffset == -1 ? nullptr : UsePlayset->Get<UObject*>(SaveRecordOffset)), CookedOffset == -1 ? -1 : (int)UsePlayset->Get<bool>(CookedOffset),
				SizeXOffset == -1 ? -1 : UsePlayset->Get<int>(SizeXOffset), SizeXOffset == -1 ? -1 : UsePlayset->Get<int>(SizeXOffset + 4), SizeXOffset == -1 ? -1 : UsePlayset->Get<int>(SizeXOffset + 8),
				PropsOffset == -1 ? -1 : *(int*)((char*)UsePlayset + PropsOffset + 8));
		}

		InspectPlayset(UsePlayset);

		// Step 0: the playset's own function that spawns its actors around an actor (here the prefab volume). If the game's code for it is present, the
		// actors are ordinary replicated actors and every client sees them. Judged by whether the number of actors in the world went up.
		bool bStep0Spawned = false;
		{
			static auto PlaysetDefault = FindObject<UObject>(L"/Script/FortniteGame.Default__FortPlaysetItemDefinition");
			const int Before = CountActors(L"/Script/Engine.Actor");

			if (PlaysetDefault)
			{
				if (auto SpawnActorsFn = PlaysetDefault->FindFunction("SpawnActorsInPlayset"))
				{
					struct { AActor* WorldContextObject; UFortPlaysetItemDefinition* Playset; } Params{ Volume, UsePlayset };
					CreativeIsland::StartActorProbe("SpawnActorsInPlayset", Volume); // the spawn may be asynchronous, the count right after the call can miss it
					LOG_INFO(LogCreative, "Thrown prefab: step 0, calling SpawnActorsInPlayset on {} ({} actors in the world)", Name, Before);
					PlaysetDefault->ProcessEvent(SpawnActorsFn, &Params);
					const int After = CountActors(L"/Script/Engine.Actor");
					LOG_INFO(LogCreative, "Thrown prefab: step 0 done, {} actors in the world now ({:+})", After, After - Before);
					bStep0Spawned = After > Before;
				}
				else
					LOG_WARN(LogCreative, "Thrown prefab: step 0 skipped, FortPlaysetItemDefinition has no SpawnActorsInPlayset");
			}
			else
				LOG_WARN(LogCreative, "Thrown prefab: step 0 skipped, Default__FortPlaysetItemDefinition not found");
		}

		LOG_INFO(LogCreative, "Thrown prefab: {} (playset {} from {}) at {} is in state {}, stream component {}", Name, UsePlayset->GetName(), Source, Where(Volume), (int)Volume->GetVolumeState(), HasStream() ? "present" : "missing");

		if (bStep0Spawned)
		{
			LOG_INFO(LogCreative, "Thrown prefab: step 0 placed the prefab, the other steps are skipped");
			++Loaded;
			continue;
		}

		// Our own version of the prefab, built from building pieces.
		if (Globals::bOwnPrefabs)
		{
			int Built = BuildPrefabFromClasses(Volume, UsePlayset);

			if (Built <= 0)
				Built = BuildOwnPrefab(Volume, UsePlayset); // the wood house

			if (Built <= 0)
								Built = BuildOwnDevice(Volume->GetActorLocation(), UsePlayset);

			if (Built > 0)
			{
				CreativeIsland::LogBudget("after an own prefab");
				++Loaded;
				continue;
			}
		}

		// Step 0b: is the volume manager waiting for something? (read-only unless it is stuck "loading")
		CheckVolumeManager(Volume);

		// Step 0c: what the prefab's props are stored as (read-only), so the layout of the records can be worked out from the log.
		{
			static int Described = 0;

			if (Described++ < 4)
			{
				std::vector<UObject*> Records;
				PlaysetRecords(UsePlayset, Records, true);

				for (size_t i = 0; i < Records.size() && i < 2; ++i)
					DescribeRecord("Prefab data: record " + std::to_string(i), Records[i]);
			}
		}

		// Step 0d (own tick box): apply the first records with the volume manager's own function and watch where actors appear.
		if (Globals::bPrefabApplyRecords)
			ApplyPrefabRecords(UsePlayset, Volume);

		// Step 1: the game's native SpawnVolume sets the volume's playset directly and skips the SetCurrentPlayset function, which is the one that
		// would set the volume up for loading. Call that function (clearing the field first, so it does not see "nothing changed").
		static auto VolumePlaysetFieldOffset = Volume->GetOffset("CurrentPlayset", false);

		if (VolumePlaysetFieldOffset != -1)
			Volume->Get<UObject*>(VolumePlaysetFieldOffset) = nullptr;

		Volume->SetCurrentPlayset(UsePlayset);
		LOG_INFO(LogCreative, "Thrown prefab: SetCurrentPlayset called; {} is in state {}, stream component {}", Name, (int)Volume->GetVolumeState(), HasStream() ? "present" : "missing");

		// Step 2: only if the volume now has a stream component can the playset be streamed into it.
		if (HasStream())
		{
			UFortPlaysetItemDefinition::ShowPlayset(UsePlayset, Volume);
			LOG_INFO(LogCreative, "Thrown prefab: streamed the playset into {}", Name);
		}

		// Step 2b (own tick box): the island setup marks its volume Ready and tells the clients, and a client loads a playset into a volume that is Ready.
		if (Globals::bPrefabMarkReady)
		{
			LOG_INFO(LogCreative, "Thrown prefab: marking {} Ready (it is in state {})", Name, (int)Volume->GetVolumeState());
			Volume->GetVolumeState() = EVolumeState::Ready;
			CallOnRep(Volume, "OnRep_VolumeState");
			CallOnRep(Volume, "OnRep_CurrentPlayset");
			LOG_INFO(LogCreative, "Thrown prefab: {} is now in state {}, stream component {}", Name, (int)Volume->GetVolumeState(), HasStream() ? "present" : "missing");
		}

		// Step 3: the volume manager's queue, as the game does when a volume is waiting.

		// The prefab volume has no stream component of its own and starts Uninitialized (0): the game activates such volumes from the volume manager's
		// queue (TryActivateQueuedVolumes). Nothing is forced on the volume here, in earlier tests forcing it was followed by a crash.
		static auto ManagerClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolumeManager");
		static auto GameStateClass = FindObject<UClass>(L"/Script/FortniteGame.FortGameStateAthena");

		auto Managers = ManagerClass ? UGameplayStatics::GetAllActorsOfClass(GetWorld(), ManagerClass) : TArray<AActor*>();
		auto States = GameStateClass ? UGameplayStatics::GetAllActorsOfClass(GetWorld(), GameStateClass) : TArray<AActor*>();

		if (Managers.Num() > 0 && States.Num() > 0)
		{
			auto Manager = Managers.at(0);
			auto GameState = States.at(0);

			static auto GamePhaseOffset = GameState->GetOffset("GamePhase", false);
			const uint8_t Phase = GamePhaseOffset == -1 ? 0 : GameState->Get<uint8_t>(GamePhaseOffset);

			if (auto ActivateFn = Manager->FindFunction("TryActivateQueuedVolumes"))
			{
				Manager->ProcessEvent(ActivateFn, (void*)&Phase);
				LOG_INFO(LogCreative, "Thrown prefab: TryActivateQueuedVolumes(game phase {}) called; {} is now in state {}", (int)Phase, Name, (int)Volume->GetVolumeState());
			}
			else
				LOG_WARN(LogCreative, "Thrown prefab: the volume manager has no TryActivateQueuedVolumes");
		}
		else
			LOG_WARN(LogCreative, "Thrown prefab: no volume manager ({}) or game state ({}) found", Managers.Num(), States.Num());

		if (ManagerClass) Managers.Free();
		if (GameStateClass) States.Free();
		++Loaded;
	}

	Volumes.Free();

	if (!Loaded)
		LOG_WARN(LogCreative, "Thrown prefab: no new prefab volume was found to load the playset into");
}

void CreativeIsland::LogMinigameStates(const char* Why)
{
	static auto MinigameClass = FindObject<UClass>(L"/Script/FortniteGame.FortMinigame");

	if (!MinigameClass)
		return;

	static const char* StateNames[] = { "PreGame", "Setup", "Transitioning", "WaitingForCameras", "Warmup", "InProgress", "PostGameTimeDilation", "PostRoundEnd", "PostGameEnd", "PostGameAbandon", "PostGameReset" };
	static std::map<std::string, int> LastState;

	auto Minigames = UGameplayStatics::GetAllActorsOfClass(GetWorld(), MinigameClass);

	for (int i = 0; i < Minigames.Num(); ++i)
	{
		auto Minigame = Minigames.at(i);

		if (!Minigame)
			continue;

		static auto CurrentStateOffset = Minigame->GetOffset("CurrentState", false);

		if (CurrentStateOffset == -1)
			continue;

		const int State = (int)Minigame->Get<uint8_t>(CurrentStateOffset);
		const auto Name = Minigame->GetName();
		auto Found = LastState.find(Name);

		if (Found == LastState.end())
			Found = LastState.emplace(Name, 0).first; // everything starts in PreGame

		if (Found->second == State)
			continue;

		LOG_INFO(LogCreative, "Minigame {} went from {} to {} ({})", Name, Found->second >= 0 && Found->second <= 10 ? StateNames[Found->second] : "?", State >= 0 && State <= 10 ? StateNames[State] : "?", Why);
		Found->second = State;
	}

	Minigames.Free();
}

void CreativeIsland::LogBudget(const char* Why, int MaxTimes)
{
	static int Times = 0;

	if (Times++ >= MaxTimes)
		return;

	static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");
	static auto TrackingClass = FindObject<UClass>("/Script/FortniteGame.FortVolumeObjectTrackingComponent");
	static const char* Categories[] = { "Memory", "Simulation", "Rendering", "Network", "Audio" };

	if (!ControllerClass || !TrackingClass)
		return;

	// Where the per-asset costs come from: the volume manager points at a cost directory asset. If it is not loaded, nothing has a cost.
	{
		static auto ManagerClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolumeManager");
		static bool bTriedLoad = false;

		auto Managers = ManagerClass ? UGameplayStatics::GetAllActorsOfClass(GetWorld(), ManagerClass) : TArray<AActor*>();

		if (Managers.Num() > 0)
		{
			auto Manager = Managers.at(0);
			static auto CostDirectoryOffset = Manager->GetOffset("AssetCostDirectory", false);

			if (CostDirectoryOffset != -1)
			{
				auto& CostDirectory = Manager->Get<TSoftObjectPtr<UObject>>(CostDirectoryOffset);
				const bool bSet = CostDirectory.IsValid();
				auto Loaded = bSet ? CostDirectory.Get() : nullptr;

				if (bSet && !Loaded && !bTriedLoad)
				{
					bTriedLoad = true;
					Loaded = CostDirectory.Get(FindObject<UClass>(L"/Script/FortniteGame.CreativeAssetCostDirectory"), true);
					LOG_INFO(LogCreative, "Budget: the cost directory was not loaded, tried to load it: {}", Loaded ? "loaded" : "failed");
				}

				LOG_INFO(LogCreative, "Budget ({}): AssetCostDirectory is {}, {}", Why, bSet ? CostDirectory.SoftObjectPtr.ObjectID.AssetPathName.ToString() : std::string("not set"), Loaded ? "loaded (" + Loaded->GetName() + ")" : std::string("NOT loaded"));
			}
		}

		if (ManagerClass)
			Managers.Free();
	}

	auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

	for (int c = 0; c < Controllers.Num(); ++c)
	{
		auto Controller = (AFortPlayerControllerAthena*)Controllers.at(c);
		auto Volume = Controller ? Controller->GetCreativePlotLinkedVolume() : nullptr;
		auto Tracking = Volume ? (UObject*)Volume->GetComponentByClass(TrackingClass) : nullptr;

		if (!Tracking)
		{
			LOG_INFO(LogCreative, "Budget ({}): {} has no object tracking component on its island volume", Why, Describe(Controller));
			continue;
		}

		static auto TrackerOffset = Tracking->GetOffset("BudgetTracker", false);
		static auto RunNewVersionOffset = Tracking->GetOffset("bRunNewVersion", false);
		static auto HeatmapOffset = Tracking->GetOffset("bHeatmapEnabled", false);

		if (TrackerOffset == -1)
		{
			LOG_WARN(LogCreative, "Budget ({}): no BudgetTracker property on the tracking component", Why);
			continue;
		}

		// FFortCreativeBudgetTracker keeps its TArray<FFortCreativeBudget> at +0x10; each entry is 0x14 bytes: TotalBudget, UsedBudget, Category (uint8 at +8), FixedInstanceCost (+0x10).
		auto Array = (char*)Tracking + TrackerOffset + 0x10;
		auto Data = *(char**)Array;
		const int Num = *(int*)(Array + 8);

		std::string Text = std::format("Budget ({}), bRunNewVersion {}, heatmap {}: {} categories", Why, RunNewVersionOffset == -1 ? -1 : (int)Tracking->Get<bool>(RunNewVersionOffset), HeatmapOffset == -1 ? -1 : (int)Tracking->Get<bool>(HeatmapOffset), Num);

		for (int i = 0; Data && i < Num && i < 8; ++i)
		{
			auto Entry = Data + i * 0x14;
			const int Category = *(uint8_t*)(Entry + 8);
			Text += std::format(" | {} used {} of {} (fixed instance cost {})", Category >= 0 && Category <= 4 ? Categories[Category] : "?", *(int*)(Entry + 4), *(int*)Entry, *(int*)(Entry + 0x10));
		}

		LOG_INFO(LogCreative, "{}", Text);
	}

	Controllers.Free();
}


// ---------------------------------------------------------------------------------------------------------------------
// CreativeMinigame
// ---------------------------------------------------------------------------------------------------------------------
namespace CreativeMinigame
{
	static DWORD GameThreadId = 0;
	static std::map<UObject*, ULONGLONG> EndRequests; // controller -> when it pressed End Game

	enum EState { PreGame = 0, Setup = 1, Transitioning = 2, WaitingForCameras = 3, Warmup = 4, InProgress = 5, PostGameTimeDilation = 6, PostRoundEnd = 7, PostGameEnd = 8, PostGameAbandon = 9, PostGameReset = 10 };

	static const char* NameOf(int State)
	{
		static const char* Names[] = { "PreGame", "Setup", "Transitioning", "WaitingForCameras", "Warmup", "InProgress", "PostGameTimeDilation", "PostRoundEnd", "PostGameEnd", "PostGameAbandon", "PostGameReset" };
		return State >= 0 && State <= 10 ? Names[State] : "?";
	}

	struct Driver
	{
		int State = -1;
		ULONGLONG Since = 0;
		ULONGLONG LastDriven = 0;
		bool bAdded = false;
		bool bPossessed = false;
		bool bCameras = false;
		bool bStartedHandled = false;
		bool bAdvanced = false;
		bool bLimitCalled = false;
		bool bEndCalled = false;
		bool bResetDone = false;
	};

	static std::map<UObject*, Driver> Drivers;

	static bool Call(UObject* Object, const char* Function, void* Params = nullptr)
	{
		if (!Object)
			return false;

		auto Fn = Object->FindFunction(Function);

		if (!Fn)
		{
			LOG_WARN(LogCreative, "Minigame driver: {} has no function {}", Object->GetName(), Function);
			return false;
		}

		Object->ProcessEvent(Fn, Params);
		return true;
	}

	static int StateOf(UObject* Minigame)
	{
		static auto Offset = Minigame->GetOffset("CurrentState", false);
		return Offset == -1 ? -1 : (int)Minigame->Get<uint8_t>(Offset);
	}

	static UObject* IslandMinigame(AFortVolume* Volume)
	{
		static auto ManagerDefault = FindObject<UObject>(L"/Script/FortniteGame.Default__FortVolumeManager");

		if (!ManagerDefault || !Volume)
			return nullptr;

		auto Fn = ManagerDefault->FindFunction("GetMinigameForVolume");

		if (!Fn)
			return nullptr;

		struct { AFortVolume* Volume; UObject* ReturnValue; } Params{ Volume, nullptr };
		ManagerDefault->ProcessEvent(Fn, &Params);
		return Params.ReturnValue;
	}

	static UObject* Manager()
	{
		static auto ManagerClass = FindObject<UClass>(L"/Script/FortniteGame.FortVolumeManager");

		if (!ManagerClass)
			return nullptr;

		auto Managers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ManagerClass);
		UObject* Found = Managers.Num() > 0 ? Managers.at(0) : nullptr;
		Managers.Free();
		return Found;
	}

	// Sets the state the way the game's own setter would: the field, then its OnRep (which runs the minigame logic components), then the handler for that state.
	static void ForceState(UObject* Minigame, int NewState, const char* Why)
	{
		static auto CurrentOffset = Minigame->GetOffset("CurrentState", false);
		static auto PreviousOffset = Minigame->GetOffset("PreviousState", false);

		if (CurrentOffset == -1)
			return;

		const int Old = (int)Minigame->Get<uint8_t>(CurrentOffset);

		LOG_WARN(LogCreative, "Minigame driver: forcing {} from {} to {} ({})", Minigame->GetName(), NameOf(Old), NameOf(NewState), Why);

		if (PreviousOffset != -1)
			Minigame->Get<uint8_t>(PreviousOffset) = (uint8_t)Old;

		Minigame->Get<uint8_t>(CurrentOffset) = (uint8_t)NewState;
		CreativeIsland::CallOnRep(Minigame, "OnRep_CurrentState");

		if (NewState == Warmup)
			Call(Minigame, "HandleMinigameWarmup");
		else if (NewState == InProgress)
		{
			auto& D = Drivers[Minigame];

			if (!D.bStartedHandled)
			{
				D.bStartedHandled = true;
				Call(Minigame, "HandleMinigameStarted");
			}
		}
		else if (NewState == PostGameEnd || NewState == PostRoundEnd)
			Call(Minigame, "HandleMinigameEnded");
	}

	static void Advance(UObject* Minigame, const char* Why)
	{
		const int Before = StateOf(Minigame);
		Call(Minigame, "AdvanceState");
		LOG_INFO(LogCreative, "Minigame driver: AdvanceState on {} ({}): {} -> {}", Minigame->GetName(), Why, NameOf(Before), NameOf(StateOf(Minigame)));
	}

	// The island a player owns is the volume of their portal. CreativePlotLinkedVolume is not used for this: it follows the player into the hub, and the
	// hub has a minigame of its own that must be left alone.
	static AFortVolume* IslandVolumeOf(AFortPlayerControllerAthena* Controller)
	{
		static auto OwnedPortalOffset = Controller ? Controller->GetOffset("OwnedPortal", false) : -1;

		if (!Controller || OwnedPortalOffset == -1)
			return nullptr;

		auto Portal = Controller->Get<AFortAthenaCreativePortal*>(OwnedPortalOffset);
		return Portal ? Portal->GetLinkedVolume() : nullptr;
	}

	static std::vector<AFortPlayerControllerAthena*> ControllersOn(AFortVolume* Volume)
	{
		std::vector<AFortPlayerControllerAthena*> Result;
		static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");

		if (!ControllerClass)
			return Result;

		auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

		for (int i = 0; i < Controllers.Num(); ++i)
		{
			auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);

			if (Controller && IslandVolumeOf(Controller) == Volume)
				Result.push_back(Controller);
		}

		Controllers.Free();
		return Result;
	}

	// The player start plate (the "Player Start" device) nearest to a pawn, or null if the island has none.
	static AActor* NearestStartPlate(AActor* Near)
	{
		static auto PlateClass = FindObject<UClass>(L"/Game/Athena/Playset/MinigamePlayerStartPlate.MinigamePlayerStartPlate_C");

		if (!PlateClass || !Near)
			return nullptr;

		auto Plates = UGameplayStatics::GetAllActorsOfClass(GetWorld(), PlateClass);
		AActor* Best = nullptr;
		double BestDistance = 0;
		const auto From = Near->GetActorLocation();

		for (int i = 0; i < Plates.Num(); ++i)
		{
			auto Plate = Plates.at(i);

			if (!Plate)
				continue;

			const auto To = Plate->GetActorLocation();
			const double dx = (double)To.X - (double)From.X, dy = (double)To.Y - (double)From.Y, dz = (double)To.Z - (double)From.Z;
			const double Distance = dx * dx + dy * dy + dz * dz;

			if (!Best || Distance < BestDistance)
			{
				Best = Plate;
				BestDistance = Distance;
			}
		}

		Plates.Free();
		return Best;
	}

	// What Xenon's FortMinigame.cpp does when the island's minigame reaches these states (the game's own server code for it is missing here):
	// each participant gets a team (3 and up) and the "pawn possessed during transition" call.
	static void PossessDuringTransition(UObject* Minigame, const std::vector<AFortPlayerControllerAthena*>& Players, bool bSetTeams)
	{
		int Index = 0;

		for (auto Player : Players)
		{
			auto Pawn = Player->GetMyFortPawn();

			if (bSetTeams)
			{
				uint8_t Team = (uint8_t)(3 + Index);
				Call(Player, "ServerSetTeam", &Team);
				LOG_INFO(LogCreative, "Minigame driver: {} is on team {}", Describe(Player), (int)Team);
			}

			++Index;

			if (!Pawn)
			{
				LOG_WARN(LogCreative, "Minigame driver: {} has no pawn for the transition", Describe(Player));
				continue;
			}

			const auto Before = Pawn->GetActorLocation();
			const std::string BeforeText = std::format("{} {} {}", Before.X, Before.Y, Before.Z);
			struct { UObject* Pawn; } Params{ Pawn };
			Call(Minigame, "OnPlayerPawnPossessedDuringTransition", &Params);
			const auto After = Pawn->GetActorLocation();
			const double dx = (double)After.X - (double)Before.X, dy = (double)After.Y - (double)Before.Y, dz = (double)After.Z - (double)Before.Z;
			const bool bMoved = dx * dx + dy * dy + dz * dz > 100.0 * 100.0;

			LOG_INFO(LogCreative, "Minigame driver: OnPlayerPawnPossessedDuringTransition for {}: the pawn {} ({} -> {})", Describe(Pawn), bMoved ? "was moved by the game" : "was not moved", BeforeText, Where(Pawn));

			// If the game left the pawn where it was: on a Player Start plate when the island has one; else as the island's spawn location setting says (Sky: where the minigame
			// puts a player in the air; Current Location: where the pawn is), and with the setting on Spawn Pads and no plate, at the island's start on the ground.
			// The 7 Nov log: the minigame's own choice with no plates is 15000 up, the pawn was still falling when the game ended, outside the island's volume,
			// and the game went from PostGameEnd to PostGameReset in the same millisecond: no end screen.
			if (!bMoved)
			{
				std::string Setting = "?";
				{
					auto SettingProperty = (const char*)Minigame->GetProperty("SpawnLocationSetting", false);
					const int SettingOffset = Minigame->GetOffset("SpawnLocationSetting", false);

					if (SettingProperty && SettingOffset != -1)
						Setting = PropDump::EnumName(*(const void* const*)(SettingProperty + PropDump::InnerAt), *((const unsigned char*)Minigame + SettingOffset));
				}

				auto Plate = NearestStartPlate(Pawn);
				const bool bAir = LowerText(Setting) == "air" || LowerText(Setting) == "sky";
				const bool bCurrent = LowerText(Setting) == "currentlocation";
				LOG_INFO(LogCreative, "Minigame driver: spawn location setting {}, {}", Setting, Plate ? "the island has a player start plate" : "the island has no player start plate");

				if (!Plate && !bCurrent)
				{
					struct { UObject* PlayerState; FVector Location; FRotator Rotation; bool bOnGround; bool ReturnValue; } Spawn{ Player->GetPlayerState(), FVector(), FRotator(), false, false };
					const bool bAsked = bAir && Call(Minigame, "DetermineSpawnLocation", &Spawn);

					FVector StartLocation;
					FRotator StartRotation;

					if (bAsked && Spawn.ReturnValue && (Spawn.Location.X != 0.f || Spawn.Location.Y != 0.f))
					{
						Pawn->TeleportTo(Spawn.Location, Spawn.Rotation);
						LOG_INFO(LogCreative, "Minigame driver: moved {} to the minigame's spawn location {}", Describe(Pawn), Where(Pawn));
					}
					else if (auto Volume = IslandVolumeOf(Player); Volume && FindIslandStart(Volume, StartLocation, StartRotation))
					{
						Pawn->TeleportTo(StartLocation, StartRotation);
						LOG_INFO(LogCreative, "Minigame driver: moved {} to the island's start {}", Describe(Pawn), Where(Pawn));
					}
					else
					{
						LOG_INFO(LogCreative, "Minigame driver: no spawn location found for {}, it stays where it is", Describe(Pawn));
					}
				}
				else if (Plate)
				{
					auto Location = Plate->GetActorLocation();
					Location.Z += 150;
					Pawn->TeleportTo(Location, Pawn->GetActorRotation());
					LOG_INFO(LogCreative, "Minigame driver: moved {} to the player start plate {} at {}", Describe(Pawn), Plate->GetName(), Where(Plate));
				}
				else
					LOG_INFO(LogCreative, "Minigame driver: the island has no player start plate, {} stays where it is", Describe(Pawn));
			}
		}
	}

	void NoteGameThread()
	{
		GameThreadId = GetCurrentThreadId();
	}

	void NoteEndRequested(UObject* Controller)
	{
		EndRequests[Controller] = GetTickCount64();
		LOG_INFO(LogCreative, "Minigame driver: {} pressed End Game", Controller ? Controller->GetName() : std::string("null"));
	}

	static void Drive(UObject* Minigame, AFortVolume* Volume, AFortPlayerControllerAthena* Controller, ULONGLONG Now)
	{
		auto& D = Drivers[Minigame];

		if (D.LastDriven == Now)
			return; // two players on one island

		D.LastDriven = Now;

		const int State = StateOf(Minigame);

		if (State < 0)
			return;

		if (State != D.State)
		{
			static auto PlayerArrayOffset = Minigame->GetOffset("PlayerArray", false);
			static auto TimeLimitOffset = Minigame->GetOffset("TimeLimit", false);
			static auto WarmupOffset = Minigame->GetOffset("WarmupDuration", false);
			static auto RoundOffset = Minigame->GetOffset("CurrentRound", false);
			static auto TotalRoundsOffset = Minigame->GetOffset("TotalRounds", false);

			// FMinigamePlayers is a fast array whose TArray<FMinigamePlayer> Players sits at +0x108.
			const int Players = PlayerArrayOffset == -1 ? -1 : *(int*)((char*)Minigame + PlayerArrayOffset + 0x108 + 8);

			LOG_INFO(LogCreative, "Minigame driver: {} is now {} (was {}); players in it {}, TimeLimit {}, WarmupDuration {}, round {} of {}", Minigame->GetName(), NameOf(State), D.State < 0 ? "unknown" : NameOf(D.State), Players,
				TimeLimitOffset == -1 ? -1.f : Minigame->Get<float>(TimeLimitOffset), WarmupOffset == -1 ? -1.f : Minigame->Get<float>(WarmupOffset), RoundOffset == -1 ? -1 : Minigame->Get<int>(RoundOffset), TotalRoundsOffset == -1 ? -1 : Minigame->Get<int>(TotalRoundsOffset));

			D.State = State;
			D.Since = Now;
			D.bAdvanced = false;

				for (auto Player : ControllersOn(Volume))
				{
					struct { UObject* PlayerState; bool ReturnValue; } Inside{ Player->GetPlayerState(), false };

					if (Call(Minigame, "IsPlayerInOurVolume", &Inside))
						LOG_INFO(LogCreative, "Minigame driver: {} is {} the island's volume at {}", Describe(Player), Inside.ReturnValue ? "INSIDE" : "OUTSIDE", Player->GetMyFortPawn() ? Where(Player->GetMyFortPawn()) : std::string("(no pawn)"));
				}

			if (State == Transitioning) { D.bAdded = false; D.bPossessed = false; D.bStartedHandled = false; }
			if (State == WaitingForCameras) D.bCameras = false;
			if (State == InProgress) { D.bLimitCalled = false; D.bEndCalled = false; }
			if (State == PostGameReset) D.bResetDone = false;
			if (State == PreGame) D.bStartedHandled = false;
		}

		const ULONGLONG Elapsed = Now - D.Since;
		auto Players = ControllersOn(Volume);

		// End Game was pressed: the game's own EndGame is given a moment, then called.
		auto Request = EndRequests.find(Controller);

		if (Request != EndRequests.end())
		{
			if (State >= PostGameTimeDilation || State == PreGame)
				EndRequests.erase(Request);
			else if (Now - Request->second > 1500)
			{
				LOG_INFO(LogCreative, "Minigame driver: End Game was pressed {} ms ago and {} is still {}, calling EndGame", (int)(Now - Request->second), Minigame->GetName(), NameOf(State));
				struct { UObject* Instigator; uint8_t Method; } Params{ Controller, 1 /* EndGame */ };
				Call(Minigame, "EndGame", &Params);
				LOG_INFO(LogCreative, "Minigame driver: after EndGame it is {}", NameOf(StateOf(Minigame)));
				EndRequests.erase(Request);

				if (StateOf(Minigame) < PostGameTimeDilation)
					ForceState(Minigame, PostGameEnd, "EndGame did nothing");

				return;
			}
		}

		switch (State)
		{
		case Setup:
			if (Elapsed > 3000 && !D.bAdvanced) { D.bAdvanced = true; Advance(Minigame, "stuck in Setup"); }
			break;

		case Transitioning:
		{
			// 1. everyone in the island is a participant (the game's list is asked first)
			if (Elapsed > 600 && !D.bAdded)
			{
				D.bAdded = true;

				struct { TArray<UObject*> Out; } Participants{};
				Call(Minigame, "GetParticipatingPlayers", &Participants);
				LOG_INFO(LogCreative, "Minigame driver: the game lists {} participating players, {} players are on the island", Participants.Out.Num(), (int)Players.size());

				for (auto Player : Players)
				{
					auto PlayerState = Player->GetPlayerState();
					bool bListed = false;

					for (int i = 0; i < Participants.Out.Num(); ++i)
						bListed = bListed || Participants.Out.at(i) == PlayerState;

					if (bListed)
						continue;

					struct { UObject* PlayerState; bool bForceSpawn; } Add{ PlayerState, true };
					Call(Minigame, "AddMinigamePlayer", &Add);
					LOG_INFO(LogCreative, "Minigame driver: added {} as a participant", Describe(PlayerState));
				}

				Participants.Out.Free();
			}

			// 2. teams, and the pawns possessed for the transition
			if (Elapsed > 1200 && !D.bPossessed)
			{
				D.bPossessed = true;
				PossessDuringTransition(Minigame, Players, true);
			}

			// 3. on to the next state, then the minigame's start handler (the order Xenon uses)
			if (Elapsed > 1800 && !D.bAdvanced)
			{
				D.bAdvanced = true;
				Advance(Minigame, "Transitioning");

				if (!D.bStartedHandled)
				{
					D.bStartedHandled = true;
					Call(Minigame, "HandleMinigameStarted");
					LOG_INFO(LogCreative, "Minigame driver: HandleMinigameStarted called, state {}", NameOf(StateOf(Minigame)));
				}
			}

			if (Elapsed > 4500)
				ForceState(Minigame, Warmup, "Transitioning never ended");

			break;
		}

		case WaitingForCameras:
		{
			// the clients are told their pawns finished teleporting, then the state moves on after a second (Xenon waits 1 s as well)
			if (Elapsed > 300 && !D.bCameras)
			{
				D.bCameras = true;

				for (auto Player : Players)
				{
					if (auto Pawn = Player->GetMyFortPawn())
					{
						struct { UObject* Pawn; } Finished{ Pawn };
						Call(Minigame, "OnClientFinishTeleportingForMinigame", &Finished);
						LOG_INFO(LogCreative, "Minigame driver: OnClientFinishTeleportingForMinigame for {}", Describe(Pawn));
					}
				}
			}

			if (Elapsed > 1300 && !D.bAdvanced) { D.bAdvanced = true; Advance(Minigame, "WaitingForCameras"); }
			if (Elapsed > 4000) ForceState(Minigame, Warmup, "WaitingForCameras never ended");
			break;
		}

		case Warmup:
		{
			static auto WarmupOffset = Minigame->GetOffset("WarmupDuration", false);
			float Seconds = WarmupOffset == -1 ? 3.f : Minigame->Get<float>(WarmupOffset);
			Seconds = Seconds < 0.f ? 0.f : (Seconds > 15.f ? 15.f : Seconds);
			const ULONGLONG WarmupMs = (ULONGLONG)(Seconds * 1000.f);

			if (Elapsed > WarmupMs + 500 && !D.bAdvanced) { D.bAdvanced = true; Advance(Minigame, "Warmup over"); }
			if (Elapsed > WarmupMs + 2500) ForceState(Minigame, InProgress, "Warmup never ended");
			break;
		}

		case InProgress:
		{
			static auto TimeLimitOffset = Minigame->GetOffset("TimeLimit", false);
			// The unit of TimeLimit is not known (seconds, or the minutes shown in the settings), so a small value is read as minutes: a 5 minute game
			// must not end after 5 seconds.
			const float RawLimit = TimeLimitOffset == -1 ? 0.f : Minigame->Get<float>(TimeLimitOffset);
			const float Limit = RawLimit > 0.f && RawLimit < 30.f ? RawLimit * 60.f : RawLimit;

			if (Limit > 0.f)
			{
				const ULONGLONG LimitMs = (ULONGLONG)(Limit * 1000.f);

				if (Elapsed > LimitMs && !D.bLimitCalled) { D.bLimitCalled = true; LOG_INFO(LogCreative, "Minigame driver: the time limit ({} s) is over", Limit); Call(Minigame, "OnTimeLimitElapsed"); }

				if (Elapsed > LimitMs + 3000 && !D.bEndCalled)
				{
					D.bEndCalled = true;
					struct { UObject* Instigator; uint8_t Method; } Params{ nullptr, 1 };
					Call(Minigame, "EndGame", &Params);

					if (StateOf(Minigame) == InProgress)
						ForceState(Minigame, PostGameEnd, "time limit over, EndGame did nothing");
				}
			}
			break;
		}

		case PostGameTimeDilation:
		case PostRoundEnd:
		case PostGameEnd:
			if (Elapsed > 4000 && !D.bAdvanced) { D.bAdvanced = true; Advance(Minigame, "post game"); }
			if (Elapsed > 8000) ForceState(Minigame, PostGameReset, "post game never ended");
			break;

		case PostGameAbandon:
			if (Elapsed > 1500) ForceState(Minigame, PostGameReset, "abandoned");
			break;

		case PostGameReset:
		{
			if (!D.bResetDone)
			{
				D.bResetDone = true;
				PossessDuringTransition(Minigame, Players, false);

				if (auto VolumeManager = Manager())
				{
					struct { AFortVolume* Volume; } Params{ Volume };
					Call(VolumeManager, "MinigameExitReadOnlyForVolume", &Params);
				}

				LOG_INFO(LogCreative, "Minigame driver: players put back, island volume is now state {}", (int)Volume->GetVolumeState());
			}

			if (Elapsed > 1500)
			{
				ForceState(Minigame, PreGame, "game over, back to editing");

				if (Volume->GetVolumeState() != EVolumeState::Ready)
				{
					Volume->GetVolumeState() = EVolumeState::Ready;
					CreativeIsland::CallOnRep(Volume, "OnRep_VolumeState");
				}
			}
			break;
		}

		default:
			break;
		}
	}

	void Tick()
	{
		if (!GameThreadId || GetCurrentThreadId() != GameThreadId)
			return;

		static bool bInTick = false;
		static ULONGLONG Last = 0;

		const ULONGLONG Now = GetTickCount64();

		if (bInTick || Now - Last < 250)
			return;

		Last = Now;
		bInTick = true;

		CreativeIsland::TickProbes();

		if (Globals::bDumpCreativeDataRequested)
		{
			Globals::bDumpCreativeDataRequested = false;
			CreativeIsland::DumpCreativeData("button");
		}

		if (Globals::bSurveyPrefabsRequested)
		{
			Globals::bSurveyPrefabsRequested = false;
			CreativeIsland::StartPrefabSurvey();
		}

				if (Globals::bSpawnAllDevicesRequested)
		{
			Globals::bSpawnAllDevicesRequested = false;
			CreativeIsland::SpawnAllDevices();
		}

		CreativeIsland::StepPrefabSurvey();


		if (!Globals::bDriveMinigames)
		{
			bInTick = false;
			return;
		}

		static auto ControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortPlayerControllerAthena");

		if (ControllerClass)
		{
			auto Controllers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ControllerClass);

			for (int i = 0; i < Controllers.Num(); ++i)
			{
				auto Controller = (AFortPlayerControllerAthena*)Controllers.at(i);
				auto Volume = IslandVolumeOf(Controller);
				auto Minigame = Volume ? IslandMinigame(Volume) : nullptr;

				if (Minigame)
					Drive(Minigame, Volume, Controller, Now);
			}

			Controllers.Free();
		}

		bInTick = false;
	}
}
