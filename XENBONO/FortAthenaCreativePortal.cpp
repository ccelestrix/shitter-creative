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
#include "FortPlayerStateAthena.h"
#include <algorithm>
#include <map>
#include <set>

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

	auto Location = LinkedVolume->GetActorLocation();
	Location.Z = 10000; // the island is entered from the sky, like the Xenon server does
	LOG_INFO(LogCreative, "TeleportPlayerToLinkedVolume: sending the pawn to {} {} {}", Location.X, Location.Y, Location.Z);
	CreativeIsland::DumpState("before the teleport onto the island");
	// Location.Z -= 10000; // proper 1:1
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

void CreativeIsland::GiveCreativePhone(AFortPlayerControllerAthena* Controller)
{
	if (!Controller)
		return;

	static auto CreativePhone = FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/Prototype/WID_CreativeTool.WID_CreativeTool");

	if (!CreativePhone)
	{
		LOG_WARN(LogCreative, "WID_CreativeTool was not found, so {} gets no creative phone", Controller->GetName());
		return;
	}

	auto WorldInventory = Controller->GetWorldInventory();

	if (!WorldInventory)
		return;

	if (WorldInventory->FindItemInstance(CreativePhone))
		return;

	bool bShouldUpdate = false;
	WorldInventory->AddItem(CreativePhone, &bShouldUpdate, 1);

	if (bShouldUpdate)
		WorldInventory->Update(Controller);

	LOG_INFO(LogCreative, "Gave {} the creative phone", Controller->GetName());
	CallOnRep(Controller, "ClientCreativePhoneCreated");
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

static ABuildingSMActor* SpawnPiece(UClass* Class, double X, double Y, double Z, float Yaw, AFortPlayerControllerAthena* Builder, int Team)
{
	FTransform Transform{};
	FRotator Rotation{};
	Rotation.Yaw = Yaw;
	Transform.Translation = FVector((float)X, (float)Y, (float)Z);
	Transform.Rotation = Rotation.Quaternion();
	Transform.Scale3D = { 1, 1, 1 };

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
		Piece->SetPlayerPlaced(true);

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

enum class PieceKind { Floor, Roof, Stairs, Door, Window, Opening, Wall, Pillar, Trim, WallDecor, Prop };

static const char* KindName(PieceKind Kind)
{
	static const char* Names[] = { "floor", "roof", "stairs", "door", "window", "opening", "wall", "pillar", "trim", "wall decoration", "decoration" };
	return Names[(int)Kind];
}

static PieceKind KindOf(const std::string& Path)
{
	const auto Name = Path.substr(Path.find_last_of('/') + 1);
	auto Has = [&](const char* Word) { return ContainsNoCase(Name, Word); };

	if (Has("stair") || Has("ramp"))
		return PieceKind::Stairs;

	if (Has("roof") || Has("ceiling"))
		return PieceKind::Roof;

	if (Has("floor") || Has("ground") || Has("slab") || Has("deck"))
		return PieceKind::Floor;

	if (Has("window"))
		return PieceKind::Window;

	if (Has("doorway") || Has("arch") || Has("opening") || Has("gate"))
		return PieceKind::Opening;

	if (Has("door"))
		return PieceKind::Door;

	if (Has("pillar") || Has("column") || Has("post") || Has("pole"))
		return PieceKind::Pillar;

	if (Has("wall_light") || Has("wallart") || Has("wall_art") || Has("painting") || Has("poster") || Has("sign") || Has("banner"))
		return PieceKind::WallDecor;

	if (Has("cap") || Has("battlement") || Has("rail") || Has("trim") || Has("balcony") || Has("fence") || Has("parapet") || Has("beam") || Has("gutter"))
		return PieceKind::Trim;

	if (Has("wall"))
		return PieceKind::Wall;

	return PieceKind::Prop;
}

// What a piece measures. The pivot of a piece is not known, but its bounding box is, so every piece is placed by the middle of its box.
struct PieceInfo
{
	UClass* Class = nullptr;
	int Count = 0;
	PieceKind Kind = PieceKind::Prop;
	double OffsetX = 0, OffsetY = 0, OffsetZ = 0; // the middle of the box minus the actor's location (at yaw 0)
	double ExtentX = 256, ExtentY = 32, ExtentZ = 192; // half sizes at yaw 0
	bool bMeasured = false;

	double LongSide() const { return 2.0 * (std::max)(ExtentX, ExtentY); }
	bool LongAlongX() const { return ExtentX >= ExtentY; }
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

// Spawns the piece high above the prefab, reads its box, removes it again.
static void MeasurePiece(PieceInfo& Info, const FVector& Probe)
{
	FTransform Transform{};
	Transform.Translation = Probe;
	Transform.Scale3D = { 1, 1, 1 };

	auto Actor = GetWorld()->SpawnActor<AActor>(Info.Class, Transform);

	if (!Actor)
		return;

	double OX, OY, OZ, EX, EY, EZ;

	if (ActorBoundsOf(Actor, OX, OY, OZ, EX, EY, EZ))
	{
		Info.OffsetX = OX - (double)Probe.X;
		Info.OffsetY = OY - (double)Probe.Y;
		Info.OffsetZ = OZ - (double)Probe.Z;
		Info.ExtentX = EX; Info.ExtentY = EY; Info.ExtentZ = EZ;
		Info.bMeasured = true;
	}

	static auto DestroyFn = FindObject<UFunction>(L"/Script/Engine.Actor.K2_DestroyActor");

	if (DestroyFn)
		Actor->ProcessEvent(DestroyFn);
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
// AlongX: 1 when the piece belongs on an edge that runs along X, 0 along Y, -1 when its direction does not matter.
static ABuildingSMActor* PlaceBox(const PieceInfo& Info, double CX, double CY, double BottomZ, float Yaw, int AlongX, AFortPlayerControllerAthena* Builder, int Team)
{
	const double Radians = (double)Yaw * 3.14159265358979 / 180.0;
	const double Cos = std::cos(Radians), Sin = std::sin(Radians);
	const double RotatedX = Info.OffsetX * Cos - Info.OffsetY * Sin;
	const double RotatedY = Info.OffsetX * Sin + Info.OffsetY * Cos;

	auto Piece = SpawnPiece(Info.Class, CX - RotatedX, CY - RotatedY, BottomZ + Info.ExtentZ - Info.OffsetZ, Yaw, Builder, Team);

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

// Returns how many pieces were built; 0 when the playset lists no pieces (the wood house is used then).
static int BuildPrefabFromClasses(AFortVolume* Volume, UObject* Playset)
{
	const int SizeXOffset = Playset->GetOffset("SizeX", false);

	if (SizeXOffset == -1)
		return 0;

	auto Counts = ReadClassCounts(Playset);

	if (Counts.empty())
	{
		LOG_INFO(LogCreative, "Own prefab (real pieces): {} lists no pieces", Playset->GetName());
		return 0;
	}

	const int PropsOffset = Playset->GetOffset("AssociatedPlaysetProps", false);
	const int Props = PropsOffset == -1 || !Readable((char*)Playset + PropsOffset, 16) ? 0 : *(int*)((char*)Playset + PropsOffset + 8);
	int SizeX = Playset->Get<int>(SizeXOffset);
	int SizeY = Playset->Get<int>(SizeXOffset + 4);

	if (Props <= 0 && SizeX * SizeY <= 1)
		return 0;

	SizeX = (std::max)(2, (std::min)(SizeX, 14));
	SizeY = (std::max)(2, (std::min)(SizeY, 14));

	// the most used pieces first, at most 24 different classes (each is loaded when first needed)
	std::stable_sort(Counts.begin(), Counts.end(), [](const ClassCount& A, const ClassCount& B) { return A.Count > B.Count; });

	std::vector<PieceInfo> Pieces;
	int Failed = 0;

	for (auto& Entry : Counts)
	{
		if (Pieces.size() >= 24)
			break;

		static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");
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
		Info.Count = Entry.Count;
		Info.Kind = KindOf(Entry.Path);
		Pieces.push_back(Info);
	}

	if (Pieces.empty())
	{
		LOG_WARN(LogCreative, "Own prefab (real pieces): none of the {} classes of {} could be loaded", (int)Counts.size(), Playset->GetName());
		return 0;
	}

	const auto Center = Volume->GetActorLocation();
	const FVector Probe((float)Center.X, (float)Center.Y, (float)Center.Z + 12000.f);

	for (auto& Piece : Pieces)
		if (Piece.Kind != PieceKind::WallDecor)
			MeasurePiece(Piece, Probe);

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

	// The size of the building follows the wall panels: their length is the pitch along the edge, their height one level.
	PieceInfo* Panel = nullptr;

	for (auto& P : Pieces)
		if ((P.Kind == PieceKind::Wall || P.Kind == PieceKind::Window || P.Kind == PieceKind::Opening) && P.bMeasured && (!Panel || P.Count > Panel->Count))
			Panel = &P;

	const double Pitch = Panel ? (std::max)(256.0, (std::min)(Panel->LongSide(), 1024.0)) : 512.0;
	const double LevelHeight = Panel ? (std::max)(192.0, (std::min)(2.0 * Panel->ExtentZ, 1024.0)) : 384.0;

	const int SlotsX = (std::max)(2, (std::min)((int)std::lround(SizeX * 512.0 / Pitch), 14));
	const int SlotsY = (std::max)(2, (std::min)((int)std::lround(SizeY * 512.0 / Pitch), 14));
	const double Width = SlotsX * Pitch, Depth = SlotsY * Pitch;
	const double MinX = (double)Center.X - Width / 2.0, MinY = (double)Center.Y - Depth / 2.0;
	const double GroundZ = (double)Center.Z;

	auto Total = [&](std::initializer_list<PieceKind> Kinds) {
		int N = 0;

		for (auto& P : Pieces)
			for (auto K : Kinds)
				if (P.Kind == K)
					N += P.Count;

		return N;
	};

	const int PerLevel = 2 * (SlotsX + SlotsY);
	const int PanelCount = Total({ PieceKind::Wall, PieceKind::Window, PieceKind::Opening, PieceKind::Door });
	const int Levels = (std::max)(1, (std::min)((PanelCount + PerLevel - 1) / PerLevel, 3));

	int Built = 0;
	std::map<PieceKind, int> ByKind;

	PlacedMoved = PlacedTurned = PlacedChecked = 0;
	PlacedMaxShift = 0;

	auto Put = [&](const PieceInfo& P, double CX, double CY, double BottomZ, float Yaw, int AlongX = -1) {
		if (PlaceBox(P, CX, CY, BottomZ, Yaw, AlongX, Builder, Team))
		{
			++Built;
			++ByKind[P.Kind];
		}
	};

	// slot k of a level, going round: the south edge west to east, the east edge south to north, the north edge east to west, the west edge north to south.
	// Gives the middle of the slot and whether the edge runs along X.
	auto SlotAt = [&](int k, double& X, double& Y, bool& bAlongX) {
		if (k < SlotsX) { X = MinX + (k + 0.5) * Pitch; Y = MinY; bAlongX = true; return; }
		k -= SlotsX;
		if (k < SlotsY) { X = MinX + Width; Y = MinY + (k + 0.5) * Pitch; bAlongX = false; return; }
		k -= SlotsY;
		if (k < SlotsX) { X = MinX + Width - (k + 0.5) * Pitch; Y = MinY + Depth; bAlongX = true; return; }
		k -= SlotsX;
		X = MinX; Y = MinY + Depth - (k + 0.5) * Pitch; bAlongX = false;
	};

	// a panel's long side has to run along its edge
	auto YawFor = [&](const PieceInfo& P, bool bEdgeAlongX) -> float { return (P.LongAlongX() == bEdgeAlongX) ? 0.f : 90.f; };

	// -- the wall panels: the door in front, then the rest spread by how many of each there are
	std::vector<PieceInfo*> Panels;
	std::vector<int> Remaining;

	for (auto& P : Pieces)
		if (P.Kind == PieceKind::Wall || P.Kind == PieceKind::Window || P.Kind == PieceKind::Opening || P.Kind == PieceKind::Door)
		{
			Panels.push_back(&P);
			Remaining.push_back(P.Count);
		}

	auto TakeNext = [&](bool bDoorOnly) -> PieceInfo* {
		int Best = -1;
		double BestShare = -1;

		for (size_t i = 0; i < Panels.size(); ++i)
		{
			const bool bDoor = Panels[i]->Kind == PieceKind::Door;

			if (Remaining[i] <= 0 || bDoorOnly != bDoor)
				continue;

			const double Share = (double)Remaining[i] / Panels[i]->Count + Panels[i]->Count * 0.0001;

			if (Share > BestShare)
			{
				BestShare = Share;
				Best = (int)i;
			}
		}

		if (Best < 0)
			return nullptr;

		--Remaining[Best];
		return Panels[Best];
	};

	for (int Level = 0; Level < Levels; ++Level)
	{
		for (int k = 0; k < PerLevel; ++k)
		{
			PieceInfo* Piece = nullptr;

			if (Level == 0 && k == SlotsX / 2)
				Piece = TakeNext(true); // the door, front centre

			if (!Piece)
				Piece = TakeNext(false);

			if (!Piece)
				break;

			double X, Y;
			bool bAlongX;
			SlotAt(k, X, Y, bAlongX);
			Put(*Piece, X, Y, GroundZ + Level * LevelHeight, YawFor(*Piece, bAlongX), bAlongX ? 1 : 0);
		}
	}

	// -- pillars at the corners and the middle of each edge, one per level
	{
		std::vector<std::pair<double, double>> Spots = { { MinX, MinY }, { MinX + Width, MinY }, { MinX + Width, MinY + Depth }, { MinX, MinY + Depth },
			{ MinX + Width / 2, MinY }, { MinX + Width, MinY + Depth / 2 }, { MinX + Width / 2, MinY + Depth }, { MinX, MinY + Depth / 2 } };
		int Placed = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Pillar)
				continue;

			for (int n = 0; n < P.Count && Placed < (int)Spots.size() * Levels; ++n, ++Placed)
				Put(P, Spots[Placed % Spots.size()].first, Spots[Placed % Spots.size()].second, GroundZ + (Placed / (int)Spots.size()) * LevelHeight, 0.f);
		}
	}

	// -- floors: the ground first, the ones left over become the upper deck
	{
		int Tile = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Floor)
				continue;

			const double Cell = (std::max)(128.0, (std::min)(P.LongSide(), 2048.0));
			const int CellsX = (std::max)(1, (int)std::lround(Width / Cell)), CellsY = (std::max)(1, (int)std::lround(Depth / Cell));

			for (int n = 0; n < P.Count; ++n, ++Tile)
			{
				const int Deck = Tile / (CellsX * CellsY);
				const int At = Tile % (CellsX * CellsY);

				if (Deck > 1)
					break;

				Put(P, MinX + (At % CellsX + 0.5) * Cell, MinY + (At / CellsX + 0.5) * Cell, GroundZ + Deck * Levels * LevelHeight, 0.f);
			}
		}
	}

	// -- the roof, over the walls
	{
		int Tile = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Roof)
				continue;

			const double Cell = (std::max)(128.0, (std::min)(P.LongSide(), 2048.0));
			const int CellsX = (std::max)(1, (int)std::lround(Width / Cell)), CellsY = (std::max)(1, (int)std::lround(Depth / Cell));

			for (int n = 0; n < P.Count && Tile < CellsX * CellsY; ++n, ++Tile)
				Put(P, MinX + (Tile % CellsX + 0.5) * Cell, MinY + (Tile / CellsX + 0.5) * Cell, GroundZ + Levels * LevelHeight, 0.f);
		}
	}

	// -- trim (battlements, rails, caps) along the top edge
	{
		int Slot = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Trim)
				continue;

			for (int n = 0; n < P.Count && Slot < PerLevel; ++n, ++Slot)
			{
				double X, Y;
				bool bAlongX;
				SlotAt(Slot, X, Y, bAlongX);
				Put(P, X, Y, GroundZ + Levels * LevelHeight, YawFor(P, bAlongX), bAlongX ? 1 : 0);
			}
		}
	}

	// -- stairs inside, near the south west corner
	{
		int Placed = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Stairs)
				continue;

			for (int n = 0; n < P.Count && Placed < 2; ++n, ++Placed)
				Put(P, MinX + Pitch * (1.5 + Placed * 1.5), MinY + Pitch * 1.5, GroundZ, 0.f);
		}
	}

	// -- decoration on the ground in rows through the inside, spaced by its own size
	{
		double X = MinX + Pitch * 1.2, Y = MinY + Pitch * 1.2, RowHeight = 0;
		int Placed = 0;

		for (auto& P : Pieces)
		{
			if (P.Kind != PieceKind::Prop)
				continue;

			const double Size = (std::max)(96.0, P.LongSide()) + 48.0;

			for (int n = 0; n < P.Count && Placed < 40; ++n, ++Placed)
			{
				if (X + Size > MinX + Width - Pitch)
				{
					X = MinX + Pitch * 1.2;
					Y += RowHeight;
					RowHeight = 0;
				}

				if (Y + Size > MinY + Depth - Pitch)
					break;

				Put(P, X + Size / 2, Y + Size / 2, GroundZ, (float)((Placed % 4) * 90));
				X += Size;
				RowHeight = (std::max)(RowHeight, Size);
			}
		}
	}

	std::string Summary;

	for (auto& Entry : ByKind)
		Summary += std::format(" {}x{}", Entry.second, KindName(Entry.first));

	LOG_INFO(LogCreative, "Own prefab (real pieces): built {} pieces for {} ({} x {} slots of {:.0f}, level height {:.0f}, {} levels, {} classes loaded, {} could not be loaded):{}", Built, Playset->GetName(), SlotsX, SlotsY, Pitch,
		LevelHeight, Levels, (int)Pieces.size(), Failed, Summary);
	LOG_INFO(LogCreative, "Own prefab (real pieces): of {} pieces whose box was checked {} had to be moved (the largest by {:.0f}) and {} turned a quarter", PlacedChecked, PlacedMoved, PlacedMaxShift, PlacedTurned);

	for (auto& P : Pieces)
		LOG_INFO(LogCreative, "  piece {} x{} as {}: box {:.0f} x {:.0f} x {:.0f}{}", P.Class->GetName(), P.Count, KindName(P.Kind), 2 * P.ExtentX, 2 * P.ExtentY, 2 * P.ExtentZ, P.bMeasured ? "" : " (not measured)");

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
			if (Entry.second == "World" && Entry.first.find("/Maps/Devices/") != std::string::npos)
				DeviceMaps.push_back(Entry);

		LOG_INFO(LogCreative, "Device maps: the registry lists {} maps under /Game/Creative/Maps/Devices", (int)DeviceMaps.size());
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

		for (auto& [Name, Class] : DeviceMaps)
		{
			const auto Short = LowerText(Name.substr(Name.find_last_of('/') + 1));

			if (Short.find(LowerText(Device)) != std::string::npos && (World = TryLoad(Name, PathText)))
				break;
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

// Returns how many actors were spawned for the device (0 when none could be).
static int BuildOwnDevice(AFortVolume* Volume, UObject* Playset)
{
	const std::string Name = Playset->GetName();
	const std::string Prefix = "PID_CP_Devices_";

	if (Name.rfind(Prefix, 0) != 0)
		return 0;

	const std::string Device = Name.substr(Prefix.size());
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
	const auto Base = Volume->GetActorLocation();
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

		// a building actor is told who built it (team, build animation)
		if (BuildingActorClass && InheritsFrom(Entry.Class, BuildingActorClass))
		{
			if (Builder)
				((ABuildingActor*)Spawned_)->InitializeBuildingActor(Builder, (ABuildingActor*)Spawned_, true);

			if (Team >= 0)
				((ABuildingActor*)Spawned_)->SetTeam((unsigned char)Team);
		}

		LOG_INFO(LogCreative, "Own device {}: spawned {} ({}) at {}", Device, Entry.Class->GetName(), Describe(Spawned_), Where(Spawned_));
	}

	LOG_INFO(LogCreative, "Own device {}: {} actors spawned (builder {}, team {})", Device, Spawned, Describe(Builder), Team);
	return Spawned;
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
				Built = BuildOwnDevice(Volume, UsePlayset);

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

			// If the game left the pawn where it was and the island has a Player Start plate, the pawn goes to the plate.
			if (!bMoved)
			{
				if (auto Plate = NearestStartPlate(Pawn))
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
