#include "FortAthenaCreativePortal.h"

#include "FortPlayerPawn.h"
#include "FortPlayerControllerAthena.h"
#include "FortPlaysetItemDefinition.h"
#include "SoftObjectPtr.h"
#include "FortInventory.h"
#include "hooking.h"
#include "GameplayStatics.h"
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

	if (Index <= 0 || Index > 8000000)
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

void CreativeIsland::TickProbes()
{
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
