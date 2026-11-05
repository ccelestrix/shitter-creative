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
XENBONO_CREATIVE_DUMP_HOOK(ServerEndMinigame)
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
		LOG_INFO(LogCreative, "Thrown prefab: loading {} (from {}) into {} at {}", UsePlayset->GetName(), Source, Name, Where(Volume));
		UFortPlaysetItemDefinition::ShowPlayset(UsePlayset, Volume);
		CallOnRep(Volume, "OnRep_VolumeState");
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
