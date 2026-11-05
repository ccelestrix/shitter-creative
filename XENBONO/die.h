#pragma once

#include "xenbono.h"
#include "FortGameModeAthena.h"

#include <mutex>
#include <unordered_map>

#include "FortAthenaCreativePortal.h"

extern inline void (*SetZoneToIndexOriginal)(AFortGameModeAthena* GameModeAthena, int OverridePhaseMaybeIDFK) = nullptr;

void SetZoneToIndexHook(AFortGameModeAthena* GameModeAthena, int OverridePhaseMaybeIDFK);

// True once ProcessEvent is hooked (by Creative at startup, or by the "Log ProcessEvent" tick box), so it is never hooked twice.
extern inline bool bProcessEventHooked = false;

// In Creative, writes the blueprint functions about prefabs, volumes, grenades, minigames and islands to xenbono_creative.log, so a
// throw that does nothing can be followed. Each function is judged once, and only its first calls are written.
static inline void LogCreativeProcessEvent(UObject* Object, UFunction* Function)
{
	static std::unordered_map<UFunction*, int> CallsLeft; // -1: not interesting
	static std::mutex Lock;

	std::lock_guard<std::mutex> guard(Lock);

	auto Found = CallsLeft.find(Function);

	if (Found == CallsLeft.end())
	{
		const auto FullName = Function->GetFullName();

		auto Has = [&FullName](const char* Text) { return FullName.find(Text) != std::string::npos; };

		// Every request the client makes of the server (except the movement and camera spam), so a button that does nothing shows up.
		const bool bServerRequest = Has(":Server") && !Has("ServerMove") && !Has("ServerUpdate") && !Has("ServerTouch") && !Has("ServerAck") && !Has("ServerCheckClient") && !Has("ServerSetSpectator")
			&& !Has("ServerSendClient") && !Has("ServerNotifyLoaded") && !Has("ServerSetClientTimeStamp") && !Has("ServerCamera") && !Has("ServerRotate") && !Has("ServerPawn");

		const bool bInteresting = bServerRequest || Has("Projectile") || Has("Explod") || Has("Detonat") || (Has("Playset") || Has("Volume") || Has("Grenade") || Has("Minigame") || Has("Creative") || Has("Prefab") || Has("Plot") || Has("Island") || Has("Playspace") || Has("Thermometer") || Has("Throw") || Has("Building:") || Has("BuildingSMActor") || Has("BuildingActor"))
			&& !Has("Audio") && !Has("Sound") && !Has("Submix") && !Has("PostProcess") && !Has("Physics") && !Has("LevelStreamingVolume") && !Has("Tick") && !Has("Animation") && !Has("Anim");

		Found = CallsLeft.emplace(Function, bInteresting ? 40 : -1).first;
	}

	if (Found->second <= 0)
		return;

	Found->second--;
	LOG_INFO(LogCreative, "ProcessEvent {} on {}", Function->GetFullName(), Object->GetName());
}

// Runs after a blueprint function has run. In Creative, when a thrown prefab/gallery grenade has loaded its playset (OnLoaded_...), the
// volume it spawned still has to stream the playset in, which nothing does on this server.
static inline void CreativeAfterProcessEvent(UObject* Object, UFunction* Function, void* Parameters)
{
	// ProcessEvent is called from more than one thread, so the table is locked. It is released before anything is loaded.
	static std::unordered_map<UFunction*, bool> IsPrefabLoadedCallback;
	static std::mutex Lock;

	bool bPrefabLoaded = false;

	{
		std::lock_guard<std::mutex> guard(Lock);

		auto Found = IsPrefabLoadedCallback.find(Function);

		if (Found == IsPrefabLoadedCallback.end())
		{
			const auto FullName = Function->GetFullName();
			Found = IsPrefabLoadedCallback.emplace(Function, FullName.find("PlaysetGrenade") != std::string::npos && FullName.find(":OnLoaded_") != std::string::npos).first;
		}

		bPrefabLoaded = Found->second;
	}

	if (bPrefabLoaded)
		CreativeIsland::LoadThrownPrefab(Object, Parameters ? *(UObject**)Parameters : nullptr);
}

static inline void ProcessEventHook(UObject* Object, UFunction* Function, void* Parameters)
{
	if (!Object || !Function)
		return;

	if (IsCreativeGame())
		LogCreativeProcessEvent(Object, Function);

	if (Globals::bLogProcessEvent)
	{
		auto FunctionName = Function->GetName(); // UKismetSystemLibrary::GetPathName(Function).ToString();
		auto FunctionFullName = Function->GetFullName();
		auto ObjectName = Object->GetName();

		if (!strstr(FunctionName.c_str(), ("EvaluateGraphExposedInputs")) &&
			!strstr(FunctionName.c_str(), ("Tick")) &&
			!strstr(FunctionName.c_str(), ("OnSubmixEnvelope")) &&
			!strstr(FunctionName.c_str(), ("OnSubmixSpectralAnalysis")) &&
			!strstr(FunctionName.c_str(), ("OnMouse")) &&
			!strstr(FunctionName.c_str(), ("Pulse")) &&
			!strstr(FunctionName.c_str(), ("BlueprintUpdateAnimation")) &&
			!strstr(FunctionName.c_str(), ("BlueprintPostEvaluateAnimation")) &&
			!strstr(FunctionName.c_str(), ("BlueprintModifyCamera")) &&
			!strstr(FunctionName.c_str(), ("BlueprintModifyPostProcess")) &&
			!strstr(FunctionName.c_str(), ("Loop Animation Curve")) &&
			!strstr(FunctionName.c_str(), ("UpdateTime")) &&
			!strstr(FunctionName.c_str(), ("GetMutatorByClass")) &&
			!strstr(FunctionName.c_str(), ("UpdatePreviousPositionAndVelocity")) &&
			!strstr(FunctionName.c_str(), ("IsCachedIsProjectileWeapon")) &&
			!strstr(FunctionName.c_str(), ("LockOn")) &&
			!strstr(FunctionName.c_str(), ("GetAbilityTargetingLevel")) &&
			!strstr(FunctionName.c_str(), ("ReadyToEndMatch")) &&
			!strstr(FunctionName.c_str(), ("ReceiveDrawHUD")) &&
			!strstr(FunctionName.c_str(), ("OnUpdateDirectionalLightForTimeOfDay")) &&
			!strstr(FunctionName.c_str(), ("GetSubtitleVisibility")) &&
			!strstr(FunctionName.c_str(), ("GetValue")) &&
			!strstr(FunctionName.c_str(), ("InputAxisKeyEvent")) &&
			!strstr(FunctionName.c_str(), ("ServerTouchActiveTime")) &&
			!strstr(FunctionName.c_str(), ("SM_IceCube_Blueprint_C")) &&
			!strstr(FunctionName.c_str(), ("OnHovered")) &&
			!strstr(FunctionName.c_str(), ("OnCurrentTextStyleChanged")) &&
			!strstr(FunctionName.c_str(), ("OnButtonHovered")) &&
			!strstr(FunctionName.c_str(), ("ExecuteUbergraph_ThreatPostProcessManagerAndParticleBlueprint")) &&
			!strstr(FunctionName.c_str(), "PinkOatmeal") &&
			!strstr(FunctionName.c_str(), "CheckForDancingAtFish") &&
			!strstr(FunctionName.c_str(), ("UpdateCamera")) &&
			!strstr(FunctionName.c_str(), ("GetMutatorContext")) &&
			!strstr(FunctionName.c_str(), ("CanJumpInternal")) &&
			!strstr(FunctionName.c_str(), ("OnDayPhaseChanged")) &&
			!strstr(FunctionName.c_str(), ("Chime")) &&
			!strstr(FunctionName.c_str(), ("ServerMove")) &&
			!strstr(FunctionName.c_str(), ("OnVisibilitySetEvent")) &&
			!strstr(FunctionName.c_str(), "ReceiveHit") &&
			!strstr(FunctionName.c_str(), "ReadyToStartMatch") &&
			!strstr(FunctionName.c_str(), "K2_GetComponentToWorld") &&
			!strstr(FunctionName.c_str(), "ClientAckGoodMove") &&
			!strstr(FunctionName.c_str(), "Prop_WildWest_WoodenWindmill_01") &&
			!strstr(FunctionName.c_str(), "ContrailCheck") &&
			!strstr(FunctionName.c_str(), "B_StockBattleBus_C") &&
			!strstr(FunctionName.c_str(), "Subtitles.Subtitles_C.") &&
			!strstr(FunctionName.c_str(), "/PinkOatmeal/PinkOatmeal_") &&
			!strstr(FunctionName.c_str(), "BP_SpectatorPawn_C") &&
			!strstr(FunctionName.c_str(), "FastSharedReplication") &&
			!strstr(FunctionName.c_str(), "OnCollisionHitEffects") &&
			!strstr(FunctionName.c_str(), "BndEvt__SkeletalMesh") &&
			!strstr(FunctionName.c_str(), ".FortAnimInstance.AnimNotify_") &&
			!strstr(FunctionName.c_str(), "OnBounceAnimationUpdate") &&
			!strstr(FunctionName.c_str(), "ShouldShowSoundIndicator") &&
			!strstr(FunctionName.c_str(), "Primitive_Structure_AmbAudioComponent_C") &&
			!strstr(FunctionName.c_str(), "PlayStoppedIdleRotationAudio") &&
			!strstr(FunctionName.c_str(), "UpdateOverheatCosmetics") &&
			!strstr(FunctionName.c_str(), "StormFadeTimeline__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "BindVolumeEvents") &&
			!strstr(FunctionName.c_str(), "UpdateStateEvent") &&
			!strstr(FunctionName.c_str(), "VISUALS__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "Flash__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "SetCollisionEnabled") &&
			!strstr(FunctionName.c_str(), "SetIntensity") &&
			!strstr(FunctionName.c_str(), "Storm__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "CloudsTimeline__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "SetRenderCustomDepth") &&
			!strstr(FunctionName.c_str(), "K2_UpdateCustomMovement") &&
			!strstr(FunctionName.c_str(), "AthenaHitPointBar_C.Update") &&
			!strstr(FunctionName.c_str(), "ExecuteUbergraph_Farm_WeatherVane_01") &&
			!strstr(FunctionName.c_str(), "HandleOnHUDElementVisibilityChanged") &&
			!strstr(FunctionName.c_str(), "ExecuteUbergraph_Fog_Machine") &&
			!strstr(FunctionName.c_str(), "ReceiveBeginPlay") &&
			!strstr(FunctionName.c_str(), "OnMatchStarted") &&
			!strstr(FunctionName.c_str(), "CustomStateChanged") &&
			!strstr(FunctionName.c_str(), "OnBuildingActorInitialized") &&
			!strstr(FunctionName.c_str(), "OnWorldReady") &&
			!strstr(FunctionName.c_str(), "OnAttachToBuilding") &&
			!strstr(FunctionName.c_str(), "Clown Spinner") &&
			!strstr(FunctionName.c_str(), "K2_GetActorLocation") &&
			!strstr(FunctionName.c_str(), "GetViewTarget") &&
			!strstr(FunctionName.c_str(), "GetAllActorsOfClass") &&
			!strstr(FunctionName.c_str(), "OnUpdateMusic") &&
			!strstr(FunctionName.c_str(), "Check Closest Point") &&
			!strstr(FunctionName.c_str(), "OnSubtitleChanged__DelegateSignature") &&
			!strstr(FunctionName.c_str(), "OnServerBounceCallback") &&
			!strstr(FunctionName.c_str(), "BlueprintGetInteractionTime") &&
			!strstr(FunctionName.c_str(), "OnServerStopCallback") &&
			!strstr(FunctionName.c_str(), "Light Flash Timeline__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "MainFlightPath__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "PlayStartedIdleRotationAudio") &&
			!strstr(FunctionName.c_str(), "BGA_Athena_FlopperSpawn_") &&
			!strstr(FunctionName.c_str(), "CheckShouldDisplayUI") &&
			!strstr(FunctionName.c_str(), "Timeline_0__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "ClientMoveResponsePacked") &&
			!strstr(FunctionName.c_str(), "ExecuteUbergraph_B_Athena_FlopperSpawnWorld_Placement") &&
			!strstr(FunctionName.c_str(), "Countdown__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "OnParachuteTrailUpdated") &&
			!strstr(FunctionName.c_str(), "Moto FadeOut__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "ExecuteUbergraph_Apollo_GasPump_Valet") &&
			!strstr(FunctionName.c_str(), "GetOverrideMeshMaterial") &&
			!strstr(FunctionName.c_str(), "VendWobble__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "WaitForPawn") &&
			!strstr(FunctionName.c_str(), "FragmentMovement__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "TrySetup") &&
			!strstr(FunctionName.c_str(), "Fade Doused Smoke__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "SetPlayerToSkydive") &&
			!strstr(FunctionName.c_str(), "BounceCar__UpdateFunc") &&
			!strstr(FunctionName.c_str(), "BP_CalendarDynamicPOISelect") &&
			!strstr(FunctionName.c_str(), "OnComponentHit_Event_0") &&
			!strstr(FunctionName.c_str(), "HandleSimulatingComponentHit") &&
			!strstr(FunctionName.c_str(), "CBGA_GreenGlop_WithGrav_C") &&
			!strstr(FunctionName.c_str(), "WarmupCountdownEndTimeUpdated") &&
			!strstr(FunctionName.c_str(), "BP_CanInteract") &&
			!strstr(FunctionName.c_str(), "AthenaHitPointBar_C") &&
			!strstr(FunctionName.c_str(), "ServerFireAIDirectorEvent") &&
			!strstr(FunctionName.c_str(), "BlueprintThreadSafeUpdateAnimation") &&
			!strstr(FunctionName.c_str(), "On Amb Zap Spawn") &&
			!strstr(FunctionName.c_str(), "ServerSetPlayerCanDBNORevive") &&
			!strstr(FunctionName.c_str(), "BGA_Petrol_Pickup_C") &&
			!strstr(FunctionName.c_str(), "GetMutatorsForContextActor") &&
			!strstr(FunctionName.c_str(), "GetControlRotation") &&
			!strstr(FunctionName.c_str(), "K2_GetComponentLocation") &&
			!strstr(FunctionName.c_str(), "MoveFromOffset__UpdateFunc") &&
			!strstr(FunctionFullName.c_str(), "PinkOatmeal_GreenGlop_C") &&
			!strstr(ObjectName.c_str(), "CBGA_GreenGlop_WithGrav_C") &&
			!strstr(ObjectName.c_str(), "FlopperSpawn") &&
			!strstr(FunctionFullName.c_str(), "GCNL_EnvCampFire_Fire_C") &&
			!strstr(FunctionName.c_str(), "BlueprintGetAllHighlightableComponents") &&
			!strstr(FunctionFullName.c_str(), "Primitive_Structure_AmbAudioComponent") &&
			!strstr(FunctionName.c_str(), "ServerTriggerCombatEvent") &&
			!strstr(FunctionName.c_str(), "SpinCubeTimeline__UpdateFunc") &&
			!strstr(ObjectName.c_str(), "FortPhysicsObjectComponent") &&
			!strstr(FunctionName.c_str(), "GetTextValue") &&
			!strstr(FunctionName.c_str(), "ServerUpdateLevelVisibility") &&
			!strstr(FunctionName.c_str(), "UpdatePlayerPositionsWithinTheMID") &&
			!strstr(FunctionName.c_str(), "ExecuteUbergraph_BGA_Petrol_Pickup"))
		{
			LOG_INFO(LogDev, "Function called: {} with {}", FunctionFullName, ObjectName);
		}
	}

	Object->ProcessEvent(Function, Parameters);

	if (IsCreativeGame())
		CreativeAfterProcessEvent(Object, Function, Parameters);
}