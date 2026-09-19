#include "ShipPawn.h"

#include "AimIndicator.h"

#include "Materials/MaterialInstanceDynamic.h"

#include "BuoyancyComponent.h"
#include "BuoyancyManager.h"
#include "TimerManager.h"
#include "CannonBall.h"
#include "GunSmoke.h"
#include "Island.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Math/UnrealMathUtility.h"
#include "GameFramework/SpringArmComponent.h"
#include "GerstnerWaterWaves.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterWaves.h"
#include "WindSubsystem.h"

namespace
{
	/** Waterline pontoons, in hull space. The mesh is modelled with its origin
	 *  exactly on the waterline, so Z stays at zero for all of them. */
	struct FPontoonSpec { float X; float Y; float Radius; };

	const FPontoonSpec GPontoons[] =
	{
		{  1250.f,    0.f, 330.f },   // bow
		{   600.f,  380.f, 320.f },
		{   600.f, -380.f, 320.f },
		{  -300.f,  420.f, 320.f },
		{  -300.f, -420.f, 320.f },
		{ -1200.f,    0.f, 330.f },   // stern
	};

	/** Gun ports along one broadside, in hull space. Taken from the same
	 *  positions the barrels were modelled at in Blender. */
	const float GGunPortsX[] = { -600.f, -240.f, 120.f, 480.f };
	// Clear of the hull collision box, whose half width is 520, so a fresh
	// shot never starts life already touching the ship that fired it.
	const float GGunPortY = 640.f;
	/** Gun-port height above the hull ORIGIN, and the hull origin is the
	 *  waterline the Blender ship was drawn around (Scripts/ship.py: DRAFT 2.6 m
	 *  below, FREEBOARD 2.1 m above, BULWARK 1.15 m of rail on top of that).
	 *
	 *  It was 120, and that was wrong twice over. The guns stand ON the deck and
	 *  fire through ports cut in the bulwark, so their muzzles belong at deck
	 *  plus about seventy centimetres - 280, not 120, which put them a metre
	 *  BELOW the deck they are supposedly standing on. And because the hull was
	 *  also floating a metre too deep, the measured muzzle height above the sea
	 *  was 19 CENTIMETRES: the broadside was fired from the waterline, which is
	 *  what made the ship read as a barge with masts. */
	const float GGunPortZ = 280.f;
}

AShipPawn::AShipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// --- root: the primitive physics and buoyancy actually act on ---------
	HullCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollision"));
	SetRootComponent(HullCollision);
	HullCollision->SetBoxExtent(FVector(1550.f, 520.f, 350.f));
	HullCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HullCollision->SetCollisionObjectType(ECC_Pawn);
	HullCollision->SetCollisionResponseToAllChannels(ECR_Block);
	// The ocean's collision box is a real physics body (QueryAndPhysics,
	// object type WorldDynamic, blocks pawns; measured with SEALOG ocean
	// collision). With Block, the hull never floated: it sat INSIDE that box at
	// the depth it was born with, buoyancy carried 0.43 of the weight (measured
	// lift=0.43) and the contact carried the rest. Overlap keeps the water-body
	// overlap semantics and lets the hull move through the sea.
	HullCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	HullCollision->SetGenerateOverlapEvents(true);
	// Every physical contact the hull makes is logged (throttled): without
	// this a hull "floating" on something solid is indistinguishable from
	// a hull floating on water.
	HullCollision->SetNotifyRigidBodyCollision(true);
	// SET ON THE BODY, NOT THROUGH THE RUNTIME SETTERS. SetSimulatePhysics and
	// SetMassOverrideInKg go on to recompute mass properties, which asks the
	// body for its physical material - and a CONSTRUCTOR runs during class
	// default object construction, before GEngine exists. The engine logs that
	// as an error, seven times, every single run. The editor shrugged; the
	// COOK counts errors and refuses to produce a build, which is how a
	// complaint that had been scrolling past all along finally had to be paid.
	// Writing the same fields on BodyInstance sets exactly what the setters
	// would have set on an instance, without the runtime path.
	HullCollision->BodyInstance.bSimulatePhysics = true;
	// A hull with no sail set moves slowly enough for Chaos to put it to
	// sleep, and the buoyancy forces do not wake it: measured, the player's
	// ship froze at z=-165 with lift=1.3 and vz=0 while the enemy, under way
	// at 6 m/s, bobbed normally. A ship on the sea is never at rest.
	HullCollision->BodyInstance.SleepFamily = ESleepFamily::Custom;
	HullCollision->BodyInstance.CustomSleepThresholdMultiplier = 0.f;
	HullCollision->SetEnableGravity(true);
	HullCollision->BodyInstance.SetMassOverride(ShipMassKg, true);
	HullCollision->SetLinearDamping(0.5f);
	HullCollision->SetAngularDamping(2.0f);
	// Weight low in the hull so the ship rights itself instead of rolling over.
	// COMNudge is what SetCenterOfMass writes; setting it here skips the mass
	// recompute that asks for a physical material before GEngine exists.
	HullCollision->BodyInstance.COMNudge = FVector(0.f, 0.f, -180.f);

	// --- visible hull, no collision of its own ---------------------------
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(HullCollision);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShipMeshAsset(
		TEXT("/Game/Meshes/SM_PirateShip.SM_PirateShip"));
	if (ShipMeshAsset.Succeeded())
	{
		HullMesh->SetStaticMesh(ShipMeshAsset.Object);
	}

	// --- the rig, as something a shot can find --------------------------
	// The hull box stops at +350 and these volumes reach +2560, so until now a
	// ball at rig height passed through empty air. These two volumes give it
	// something to strike. Their floor is exactly +350, flush with the top of
	// the hull box, so hull and rig partition the height with no gap and no
	// overlap: no single ball can ever be charged twice.
	ForeRig = CreateDefaultSubobject<UBoxComponent>(TEXT("ForeRig"));
	ForeRig->SetupAttachment(HullCollision);
	ForeRig->SetBoxExtent(FVector(230.f, 525.f, 905.f));
	ForeRig->SetRelativeLocation(FVector(560.f, 0.f, 1255.f));

	MainRig = CreateDefaultSubobject<UBoxComponent>(TEXT("MainRig"));
	MainRig->SetupAttachment(HullCollision);
	MainRig->SetBoxExtent(FVector(230.f, 525.f, 1105.f));
	MainRig->SetRelativeLocation(FVector(-320.f, 0.f, 1455.f));

	for (UBoxComponent* Volume : { ForeRig.Get(), MainRig.Get() })
	{
		// QUERY ONLY is the whole guarantee. A shape component asks to be
		// welded into its parent's body by default, and welding a tall box
		// into the hull would hand the ship an inertia tensor and a centre of
		// mass six metres in the air. Query-only shapes are refused by every
		// weld path, stay out of the simulation, and are still found by a
		// scene query, which is all a cannonball needs.
		Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		// Typed as a vehicle only because nothing else in this project uses
		// that channel: it lets the shot sweep for rigs by object type, with
		// no custom channel to configure and nothing else to match.
		Volume->SetCollisionObjectType(ECC_Vehicle);
		Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
		Volume->SetGenerateOverlapEvents(false);
		Volume->SetEnableGravity(false);
		Volume->CanCharacterStepUpOn = ECB_No;
	}

	// --- buoyancy --------------------------------------------------------
	Buoyancy = CreateDefaultSubobject<UBuoyancyComponent>(TEXT("Buoyancy"));

	FBuoyancyData& Data = Buoyancy->BuoyancyData;
	Data.Pontoons.Empty();
	for (const FPontoonSpec& Spec : GPontoons)
	{
		FSphericalPontoon Pontoon;
		// BELOW the origin, not at it. A sphere of radius 320 needs 375 cm of
		// immersion to carry its share of the weight, so a sphere CENTRED on the
		// hull origin can only balance with the origin 55 cm under water - and
		// measured, with damping and waves, the ship sat at z = -72 to -115.
		// The origin is the waterline the hull was drawn around, so a ship
		// resting a metre below it is a ship drawn 30 m long and rendered as a
		// barge: deck at 1.1 m instead of 2.1, rail at 2.25 instead of 3.25.
		// Dropping the spheres puts the hull back on her own designed line.
		Pontoon.RelativeLocation = FVector(Spec.X, Spec.Y, -PontoonDropCm);
		Pontoon.Radius = Spec.Radius;
		Data.Pontoons.Add(Pontoon);
	}
	// Pontoon offsets are authored in hull space, not relative to the centre
	// of mass, which we deliberately pushed downwards.
	Data.bCenterPontoonsOnCOM = false;
	// Force per pontoon is clamp(C * submergedVolume, 0, Max) * coefficient,
	// and the six coefficients (sprung masses) sum to 1. So the cap is a cap
	// on the WHOLE ship: the old 2.5e7 could never carry 5.88e7 of weight,
	// which is why the hull rested on the ocean box instead of floating.
	// C is set so the weight is carried at the 55 cm draught the owner already
	// verified: immersion 375 cm on a 320 cm sphere is V = 8.6e7 cm^3, weighted
	// over the six spheres 8.9e7, and 5.88e7 / 8.9e7 = 0.66. The cap sits above
	// a fully submerged sphere (0.66 * 1.4e8 = 9.2e7) so it never binds and the
	// law stays pure volume, i.e. linear in the per-pontoon coefficient.
	Data.BuoyancyCoefficient = 0.66f;
	Data.BuoyancyDamp = 3000.f;
	Data.BuoyancyDamp2 = 2.f;
	Data.MaxBuoyantForce = 2.0e8f;
	Data.bApplyDragForcesInWater = true;
	Data.DragCoefficient = 8.f;
	Data.DragCoefficient2 = 0.01f;

	// --- camera ----------------------------------------------------------
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(HullCollision);
	CameraBoom->TargetArmLength = 4200.f;
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 900.f));
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 2.5f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 4.f;

	ShipCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ShipCamera"));
	ShipCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ShipCamera->FieldOfView = 85.f;

	// The hull turns with the rudder, never with the mouse.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
}

void AShipPawn::ApplyShotCamera()
{
	if (ShotCam.IsEmpty() || !CameraBoom || !ShipCamera)
	{
		return;
	}

	// Off the controller: a boom that follows control rotation frames a
	// different picture every run, which is exactly what a comparison cannot
	// have.
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;

	// arm, height, pitch, yaw off the bow, field of view
	float Arm = 4200.f, Height = 900.f, Pitch = -12.f, Yaw = 180.f, Fov = 85.f;
	const FString Cam = ShotCam.ToLower();
	if (Cam == TEXT("beam"))
	{
		// From abeam, so the whole broadside and her heel are in frame.
		Arm = 5200.f; Height = 1100.f; Pitch = -8.f; Yaw = 90.f; Fov = 70.f;
	}
	else if (Cam == TEXT("low"))
	{
		// Just off the water: this is the frame that judges the SEA, because
		// the surface fills it and the horizon is where the sky meets it.
		Arm = 3000.f; Height = 260.f; Pitch = -2.f; Yaw = 115.f; Fov = 75.f;
	}
	else if (Cam == TEXT("bow"))
	{
		Arm = 3600.f; Height = 700.f; Pitch = -8.f; Yaw = 0.f; Fov = 70.f;
	}
	else if (Cam == TEXT("far"))
	{
		Arm = 14000.f; Height = 3200.f; Pitch = -14.f; Yaw = 135.f; Fov = 55.f;
	}
	else if (Cam == TEXT("rig"))
	{
		// Close under the masts, for judging canvas and cordage.
		Arm = 1900.f; Height = 1500.f; Pitch = 8.f; Yaw = 150.f; Fov = 80.f;
	}

	// Overrides, because the named vantages are hull-relative and the SUN is
	// not. The 'beam' frame turned out to be a backlit silhouette - correct
	// rendering, useless picture - and chasing that as if it were a lighting
	// bug cost several runs. A frame for looking at is framed against the
	// light; a frame for COMPARING two builds only has to be the same twice.
	FParse::Value(FCommandLine::Get(), TEXT("ShipShotYaw="), Yaw);
	FParse::Value(FCommandLine::Get(), TEXT("ShipShotPitch="), Pitch);
	FParse::Value(FCommandLine::Get(), TEXT("ShipShotArm="), Arm);
	FParse::Value(FCommandLine::Get(), TEXT("ShipShotHeight="), Height);
	FParse::Value(FCommandLine::Get(), TEXT("ShipShotFov="), Fov);

	CameraBoom->TargetArmLength = Arm;
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, Height));
	CameraBoom->SetRelativeRotation(FRotator(Pitch, Yaw, 0.f));
	ShipCamera->FieldOfView = Fov;
	UE_LOG(LogTemp, Display,
		TEXT("SHOTCAM vantage '%s' arm=%.0f h=%.0f pitch=%.0f yaw=%.0f fov=%.0f"),
		*Cam, Arm, Height, Pitch, Yaw, Fov);
}

void AShipPawn::BeginPlay()
{
	// Every man aboard, before anything below can take one away. Done here
	// and not in the constructor because a subclass sets HandsMax in ITS
	// constructor, which runs after this class's.
	Hands = HandsMax;

	// A MERCHANT HAS NO GUNS, so she must have no gun PORTS to lose either.
	// AMerchantShipPawn's class comment says "No guns"; her constructor
	// overrode the fields the author was thinking about and left bGunDown all
	// false, so every ball that landed in the gun-deck band took the "dismount
	// a carriage" branch instead of the hull branch: the first four beam hits
	// a side cost her NO HULL AT ALL and killed three hands instead of two.
	// That happened in prize_hull - the scenario whose entire purpose is to
	// price hull damage.
	if (Allegiance == EShipAllegiance::Merchant)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			for (int32 g = 0; g < UE_ARRAY_COUNT(bGunDown[0]); ++g)
			{
				bGunDown[Side][g] = true;
			}
		}
	}

	Super::BeginPlay();

	int32 Flag = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipInheritVel="), Flag) && Flag >= 0)
	{
		bInheritShipVelocity = Flag != 0;
	}
	Flag = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipLead="), Flag) && Flag >= 0)
	{
		bLeadTarget = Flag != 0;
	}
	// OFF by default, deliberately: every gunnery measurement in this project
	// was taken without it, and a default that quietly added thirty-two ticking
	// actors to those runs would invalidate the comparisons they exist for.
	Flag = 1;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipSmoke="), Flag))
	{
		bGunSmoke = Flag != 0;
	}
	// RangeBias makes the guns fire long by a few percent. It was calibrated
	// when the shot was laid on where the target WAS, so part of what it was
	// worth may have been standing in for the lead that did not exist. Swept
	// rather than assumed.
	float Bias = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("ShipRangeBias="), Bias) && Bias > 0.f)
	{
		RangeBias = Bias;
	}
	UE_LOG(LogTemp, Display,
		TEXT("SHOTLOG %s ballistics inherit=%d lead=%d rangeBias=%.3f"),
		*GetName(), bInheritShipVelocity ? 1 : 0, bLeadTarget ? 1 : 0, RangeBias);

	if (HullCollision)
	{
		HullCollision->SetMassOverrideInKg(NAME_None, ShipMassKg, true);
		HullCollision->OnComponentHit.AddDynamic(this, &AShipPawn::OnHullHit);
	}

	// Pawn-level test flags belong to the first ships of a run. A ship
	// respawned after a defeat must not re-fire a test broadside or pin its
	// sail for a sweep. (-ShipQuitAfter lives in the game mode: a dead pawn
	// cannot quit anything.)
	if (GetWorld() && GetWorld()->GetTimeSeconds() < 5.f)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ShipFireTest="), FireTestAt);
		// The sweep, the rudder test and the screenshot all belong to the
		// PLAYER's ship alone: the enemy is spawned half a second in, inside
		// the same window, and would otherwise pin its own sail and fire its
		// own screenshot request, which overwrites the first because the
		// filename carries no suffix.
		if (IsPlayerControlled())
		{
			FParse::Value(FCommandLine::Get(), TEXT("ShipShotAfter="), ShotAfterSeconds);

			FString ShotList;
			// bShouldStopOnSeparator=false, or the parse ends at the first comma and a
			// three-frame gallery silently becomes a one-frame one. Caught by the
			// counted line: it said "1 frames requested: 8".
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipShots="), ShotList, false))
			{
				TArray<FString> Parts;
				ShotList.ParseIntoArray(Parts, TEXT(","), true);
				for (const FString& Part : Parts)
				{
					const float T = FCString::Atof(*Part.TrimStartAndEnd());
					if (T > 0.f)
					{
						ShotTimes.Add(T);
						ShotTaken.Add(false);
					}
				}
				UE_LOG(LogTemp, Display, TEXT("SHOTCAM %d frames requested: %s"),
					ShotTimes.Num(), *ShotList);
			}
			FParse::Value(FCommandLine::Get(), TEXT("ShipShotName="), ShotName);
			FParse::Value(FCommandLine::Get(), TEXT("ShipShotCam="), ShotCam);
			ApplyShotCamera();
			FParse::Value(FCommandLine::Get(), TEXT("ShipWindSweep="), WindSweepSeconds);
			int32 PolarOn = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipPolar="), PolarOn) && PolarOn > 0)
			{
				bPolarTest = true;
			}
			FParse::Value(FCommandLine::Get(), TEXT("ShipPolarStep="), PolarStepDeg);
			FParse::Value(FCommandLine::Get(), TEXT("ShipPolarDwell="), PolarDwellSeconds);
			FParse::Value(FCommandLine::Get(), TEXT("ShipRudderTest="), RudderTestAt);
			FParse::Value(FCommandLine::Get(), TEXT("ShipRunAground="), RunAgroundAt);

			// Start the player's ship already hurt in one zone, so what that
			// zone actually costs her can be measured instead of waited for:
			// a rudder hit needs a rake through the transom, which happens
			// perhaps once in a long engagement.
			// -ShipRepairShare=x: the player's hands divided from the start,
			// so a run with no one at the keyboard can still measure a repair.
			float Share = -1.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipRepairShare="), Share) && Share >= 0.f)
			{
				SetRepairShare(Share);
			}
			float RigDamage = -1.f, RudderDamage = -1.f;
			int32 GunsDown = 0;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipRigDamage="), RigDamage) && RigDamage >= 0.f)
			{
				ForeRigIntegrity = MainRigIntegrity = FMath::Clamp(RigDamage, 0.f, 1.f);
			}
			// And each mast on its own, because the panel claims to show them
			// apart and a flag that always sets both could never prove it.
			float Fore = -1.f, Main = -1.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipForeRigDamage="), Fore) && Fore >= 0.f)
			{
				ForeRigIntegrity = FMath::Clamp(Fore, 0.f, 1.f);
			}
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipMainRigDamage="), Main) && Main >= 0.f)
			{
				MainRigIntegrity = FMath::Clamp(Main, 0.f, 1.f);
			}
			RigDamage = FMath::Max3(RigDamage, Fore, Main);
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipRudderDamage="), RudderDamage) && RudderDamage >= 0.f)
			{
				RudderIntegrity = FMath::Clamp(RudderDamage, 0.f, 1.f);
			}
			if (FParse::Value(FCommandLine::Get(), TEXT("ShipGunsDown="), GunsDown) && GunsDown > 0)
			{
				for (int32 Side = 0; Side < 2; ++Side)
				{
					for (int32 g = 0; g < FMath::Min(GunsDown, 4); ++g)
					{
						bGunDown[Side][g] = true;
					}
				}
			}
			if (RigDamage >= 0.f || RudderDamage >= 0.f || GunsDown > 0)
			{
				UE_LOG(LogTemp, Display,
					TEXT("SHIPLOG %s damage test: rig=%.2f rudder=%.2f guns=%d/%d"),
					*GetName(), GetRigEfficiency(), RudderIntegrity,
					GetGunsRemaining(false), GetGunsRemaining(true));
			}
		}
		else
		{
			// The same, for the enemy. A ship that has lost half her canvas
			// cannot claw off a lee shore, which is the one situation where
			// land kills rather than inconveniences - and it cannot be
			// measured at all if only the player can be handed a crippled rig.
			// -EnemyHands=N, GUARDED ON ALLEGIANCE. This whole block is the
			// else of IsPlayerControlled(), which a merchant falls into as
			// well - so without the guard the flag would quietly short-hand
			// the convoy too, and every number would still look plausible.
			int32 EnemyHands = 0;
			if (Allegiance == EShipAllegiance::Crown
				&& FParse::Value(FCommandLine::Get(), TEXT("EnemyHands="), EnemyHands)
				&& EnemyHands > 0)
			{
				// HOW MANY ARE ABOARD, leaving her complement where it is: she
				// sails SHORT of a full crew, with berths a port can fill. It
				// used to set the complement too, which made her a small ship
				// rather than an undermanned one - and then GetHandsShort() was
				// zero, so recruiting could never be measured anywhere and
				// handsBought was a counter nailed to zero in every scenario.
				Hands = FMath::Min(EnemyHands, HandsMax);
				UE_LOG(LogTemp, Display,
					TEXT("CREWLOG %s sails short-handed: %d of %d hands"),
					*GetName(), Hands, HandsMax);
			}

			float EnemyRig = -1.f, EnemyRudder = -1.f;
			if (FParse::Value(FCommandLine::Get(), TEXT("EnemyRigDamage="), EnemyRig)
				&& EnemyRig >= 0.f)
			{
				ForeRigIntegrity = MainRigIntegrity = FMath::Clamp(EnemyRig, 0.f, 1.f);
			}
			if (FParse::Value(FCommandLine::Get(), TEXT("EnemyRudderDamage="), EnemyRudder)
				&& EnemyRudder >= 0.f)
			{
				RudderIntegrity = FMath::Clamp(EnemyRudder, 0.f, 1.f);
			}
			if (EnemyRig >= 0.f || EnemyRudder >= 0.f)
			{
				UE_LOG(LogTemp, Display,
					TEXT("SHIPLOG %s damage test: rig=%.2f rudder=%.2f guns=%d/%d"),
					*GetName(), GetRigEfficiency(), RudderIntegrity,
					GetGunsRemaining(false), GetGunsRemaining(true));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s respawned, pawn diagnostics skipped"), *GetName());
	}

	HullIntegrity = MaxHullIntegrity;

	// -EnemyHull=N starts a CROWN ship hurt. It has to be written HERE, after
	// the line above, and not with the other enemy damage flags forty lines
	// up: set there it was quietly overwritten, and the run printed
	// "sails hurt: hull 600/1000" and then a captain reporting hull 100% in
	// the very next line. Two log lines disagreeing was the only sign.
	//
	// It exists because a raider who hunts merchants is never shot at, so
	// there is otherwise no way to measure a hull being BOUGHT back. The flag
	// sets the starting condition; the measured key is what the refit put in.
	float EnemyHull = 0.f;
	if (Allegiance == EShipAllegiance::Crown
		&& FParse::Value(FCommandLine::Get(), TEXT("EnemyHull="), EnemyHull)
		&& EnemyHull > 0.f)
	{
		HullIntegrity = FMath::Min(EnemyHull, MaxHullIntegrity);
		UE_LOG(LogTemp, Display, TEXT("CREWLOG %s sails hurt: hull %.0f/%.0f"),
			*GetName(), HullIntegrity, MaxHullIntegrity);
	}

	// FILL HER. This line is why the default is not simply a number in the
	// header: Shot starts at zero, so a ship with a default magazine and no
	// flag would have sailed dry from her first tick - a mistake that would
	// have looked exactly like the feature working.
	Shot = ShotMax;

	// -Shot=N gives the PLAYER a magazine of N rounds, -EnemyShot=N a Crown
	// ship one; either at 0 makes that ship's magazine bottomless, which is how
	// the behaviour from before the default can still be measured against it.
	{
		int32 Rounds = -1;
		// ShotFlag, not Flag: there is already a local of that name in this
		// function and the project compiles shadowing as an error.
		const TCHAR* ShotFlag = (Allegiance == EShipAllegiance::Crown)
			? TEXT("EnemyShot=") : TEXT("Shot=");
		if (Allegiance != EShipAllegiance::Merchant
			&& FParse::Value(FCommandLine::Get(), ShotFlag, Rounds) && Rounds >= 0)
		{
			ShotMax = Rounds;
			Shot = Rounds;
			UE_LOG(LogTemp, Display, TEXT("SHOTLOG %s magazine: %s"),
				*GetName(), Rounds > 0
					? *FString::Printf(TEXT("%d rounds"), Rounds)
					: TEXT("bottomless (flag set to 0)"));
		}
	}

	// -ShipHullTest=N starts the PLAYER's first ship with N integrity, so a
	// sinking by real gunfire can be measured in a couple of broadsides
	// instead of seventeen hits. The enemy is never weakened.
	float HullTest = 0.f;
	if (GetWorld() && GetWorld()->GetTimeSeconds() < 5.f && IsPlayerControlled() &&
		FParse::Value(FCommandLine::Get(), TEXT("ShipHullTest="), HullTest) && HullTest > 0.f)
	{
		HullIntegrity = FMath::Min(HullTest, MaxHullIntegrity);
		UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s hull test: integrity set to %.0f"),
			*GetName(), HullIntegrity);
	}
	if (!CannonBallClass)
	{
		CannonBallClass = ACannonBall::StaticClass();
	}

	if (WindSweepSeconds > 0.f)
	{
		// Every sail set, so the polar sweep measures the rig and not the crew.
		SailTrim = 1.f;
		UE_LOG(LogTemp, Display,
			TEXT("SHIPLOG wind sweep over %.0fs, sail pinned at full"),
			WindSweepSeconds);
	}

	// Mass, centre of mass and the inertia tensor, read out of the solver
	// rather than assumed. Every yaw number in this project was derived from
	// the box formula and never once checked against what Chaos built; and
	// anything attached to this body later could change it silently.
	const FVector Inertia = HullCollision
		? HullCollision->GetInertiaTensor() : FVector::ZeroVector;
	const FVector ComLocal = HullCollision
		? GetActorTransform().InverseTransformPosition(HullCollision->GetCenterOfMass())
		: FVector::ZeroVector;
	UE_LOG(LogTemp, Display,
		TEXT("SHIPLOG %s spawned at Z=%.1f mass=%.0fkg pontoons=%d com=(%.0f,%.0f,%.0f) inertia=(%.4e,%.4e,%.4e)"),
		*GetName(), GetActorLocation().Z,
		HullCollision ? HullCollision->GetMass() : -1.f,
		Buoyancy ? Buoyancy->BuoyancyData.Pontoons.Num() : -1,
		ComLocal.X, ComLocal.Y, ComLocal.Z,
		Inertia.X, Inertia.Y, Inertia.Z);

	GetWorldTimerManager().SetTimer(WaterRegistrationTimer, this,
		&AShipPawn::DeferredWaterRegistration, 0.2f, false);
}

void AShipPawn::DeferredWaterRegistration()
{
	const int32 Bodies = RegisterWaterBodies();

	// The manager is what actually ticks buoyancy. The component registers
	// itself in its own BeginPlay; this is a belt-and-braces second attempt in
	// case that ran before the manager existed. Measured: it made no difference
	// either way, the manager is found for spawned and placed ships alike.
	// -PontoonDrop= re-seats the spheres before the manager takes them, so the
	// flotation can be calibrated against a measured resting z without a rebuild
	// per attempt. The constructor already applied the default; this overrides.
	float DropOverride = -1.f;
	if (Buoyancy && FParse::Value(FCommandLine::Get(), TEXT("PontoonDrop="), DropOverride)
		&& DropOverride >= 0.f)
	{
		for (FSphericalPontoon& P : Buoyancy->BuoyancyData.Pontoons)
		{
			P.RelativeLocation.Z = -DropOverride;
		}
		UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s pontoons re-seated to z=-%.0f"),
			*GetName(), DropOverride);
	}

	ABuoyancyManager* Manager = nullptr;
	const bool bFound =
		ABuoyancyManager::GetBuoyancyComponentManager(this, Manager) && Manager;
	if (bFound && Buoyancy)
	{
		// One registration, whichever of us got here first. Harmless if the
		// component already registered itself in its own BeginPlay.
		Manager->Unregister(Buoyancy);
		Manager->Register(Buoyancy);
	}

	UE_LOG(LogTemp, Display,
		TEXT("SHIPLOG %s registered waterbodies=%d manager=%s"),
		*GetName(), Bodies, bFound ? TEXT("yes") : TEXT("NO"));
}

UWindSubsystem* AShipPawn::GetWind() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UWindSubsystem>() : nullptr;
}

int32 AShipPawn::RegisterWaterBodies()
{
	if (!Buoyancy || !GetWorld())
	{
		return 0;
	}

	int32 Count = 0;
	for (TActorIterator<AWaterBody> It(GetWorld()); It; ++It)
	{
		UWaterBodyComponent* Component = It->GetWaterBodyComponent();
		if (!Component)
		{
			continue;
		}
		Buoyancy->EnteredWaterBody(Component);
		if (!PrimaryWaterBody)
		{
			PrimaryWaterBody = Component;
		}
		++Count;

		// A freshly spawned ocean already carries a waves object, it is just
		// empty. Checking for null skipped it silently, so test the height.
		// The sea state itself belongs to the world, not to a ship: the game
		// mode sets it before anything reads it. A ship only reports it.
		UWaterWavesBase* Existing = It->GetWaterWaves();
		UE_LOG(LogTemp, Display,
			TEXT("SHIPLOG water body '%s' registered, waves=%s height=%.1f"),
			*It->GetName(), Existing ? TEXT("yes") : TEXT("no"),
			Existing ? Existing->GetMaxWaveHeight() : 0.f);

	}
	return Count;
}

float AShipPawn::GetForwardSpeedMS() const
{
	if (!HullCollision)
	{
		return 0.f;
	}
	return FVector::DotProduct(HullCollision->GetPhysicsLinearVelocity(),
		GetActorForwardVector()) * 0.01f;
}

float AShipPawn::SailDriveCoefficient(float AbsWindAngleDeg) const
{
	// 0 deg means the wind is dead ahead, 180 deg means it is dead astern.
	// A square rig is useless upwind, best with the wind on the quarter, and
	// slightly worse dead downwind because the after sails blanket the fore.
	if (AbsWindAngleDeg <= NoGoAngleDeg)
	{
		return 0.f;
	}
	if (AbsWindAngleDeg < 90.f)
	{
		return FMath::GetMappedRangeValueClamped(
			FVector2D(NoGoAngleDeg, 90.f), FVector2D(0.f, 0.80f), AbsWindAngleDeg);
	}
	if (AbsWindAngleDeg < 140.f)
	{
		return FMath::GetMappedRangeValueClamped(
			FVector2D(90.f, 140.f), FVector2D(0.80f, 1.0f), AbsWindAngleDeg);
	}
	return FMath::GetMappedRangeValueClamped(
		FVector2D(140.f, 180.f), FVector2D(1.0f, 0.78f), AbsWindAngleDeg);
}

float AShipPawn::GetReloadRemaining(bool bStarboard) const
{
	return bStarboard ? StarboardReload : PortReload;
}

float AShipPawn::ElevationForRangeDeg(float RangeCm) const
{
	// R = v^2 sin(2 theta) / g on flat water. The launch height of a gun deck
	// is a couple of metres against hundreds of metres of range, so ignored.
	const float V = MuzzleVelocityMS * 100.f;
	const float G = 980.f;
	const float S = FMath::Clamp(RangeCm * RangeBias * G / (V * V), 0.f, 1.f);
	return FMath::RadiansToDegrees(0.5f * FMath::Asin(S));
}

AShipPawn* AShipPawn::FindTargetOnSide(bool bStarboard) const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	// ANY hull on that beam, not just a hostile one, and deliberately so: this
	// is where the PLAYER's guns are laid, and the player's choice of target is
	// made by pointing a broadside at it. A filter here would decide for her
	// which hull she is allowed to fire into - which is the whole question a
	// raider is asking when a merchant is under her lee. Read again when
	// allegiance landed and left alone on purpose.
	const FVector Beam = GetActorRightVector() * (bStarboard ? 1.f : -1.f);
	AShipPawn* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		AShipPawn* Other = *It;
		if (Other == this || Other->IsSunk())
		{
			continue;
		}
		const FVector To = Other->GetActorLocation() - GetActorLocation();
		if (FVector::DotProduct(To.GetSafeNormal2D(), Beam) <= 0.f)
		{
			continue;   // on the other side
		}
		const float Dist = To.Size2D();
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Other;
		}
	}
	return Best;
}

bool AShipPawn::FireBroadside(bool bStarboard, AActor* AimAt, bool bHigh)
{
	if (IsSinking())
	{
		return false;   // the crews are at the pumps, or in the water
	}
	float& Reload = bStarboard ? StarboardReload : PortReload;
	if (Reload > 0.f || !CannonBallClass || !GetWorld())
	{
		return false;
	}

	const FTransform HullTransform = GetActorTransform();
	const float Side = bStarboard ? 1.f : -1.f;
	const float MuzzleSpeed = MuzzleVelocityMS * 100.f;

	// Sampled BEFORE the guns fire. Read after the loop, this carried the
	// broadside's own recoil: four guns at 9e5 on 60 tonnes is 60 cm/s of
	// sideways kick, which showed up in the log as seven degrees of leeway
	// the ship was never making.
	const FVector VelBeforeRecoil = HullCollision
		? HullCollision->GetPhysicsLinearVelocity() : FVector::ZeroVector;

	// The way the BALL carries off the muzzle - zero when inheritance is off.
	// ONE vector, read by both the sites that care: the lead solver has to
	// allow for exactly the motion the shot does not carry, and deriving that
	// twice from the same flag is how the two came to disagree.
	const FVector CarriedVel = bInheritShipVelocity
		? FVector(VelBeforeRecoil.X, VelBeforeRecoil.Y, 0.f) : FVector::ZeroVector;

	const int32 SideIndex = bStarboard ? 1 : 0;

	// THE MAGAZINE, CHECKED BEFORE THE LOOP AND ALL OR NOTHING. Not inside it,
	// and this is not a stylistic choice: the loop draws FMath::FRandRange
	// twice per gun for train and elevation, off the -ShipSeed stream, so a
	// gun that quietly declined to fire would skip its draws and move every
	// ball fired afterwards in that run. Every before/after comparison this
	// project makes would be reading the seed instead of the change.
	//
	// So either the whole broadside goes or none of it does, and the loop below
	// is byte for byte the loop that was there before. She needs a round for
	// every gun that still bears; with fewer she cannot fire that side at all.
	const int32 GunsThatBear = GetGunsRemaining(bStarboard);
	if (HasMagazine() && Shot < GunsThatBear)
	{
		// Counted once per dry spell. The AI asks to fire on every tick her
		// guns bear, so counting attempts here would count frames - the defect
		// the review named in this exact function before it was written.
		if (!bReportedDry)
		{
			bReportedDry = true;
			++DryRefusals;
			UE_LOG(LogTemp, Display,
				TEXT("SHOTLOG %s DRY side=%s shot=%d guns=%d refusals=%d t=%.1f"),
				*GetName(), bStarboard ? TEXT("starboard") : TEXT("port"),
				Shot, GunsThatBear, DryRefusals, GetWorld()->GetTimeSeconds());
		}
		return false;   // and no reload is burned on a broadside that never was
	}

	int32 Fired = 0;
	for (int32 g = 0; g < UE_ARRAY_COUNT(GGunPortsX); ++g)
	{
		if (bGunDown[SideIndex][g])
		{
			continue;   // that carriage is wreckage
		}
		const float PortX = GGunPortsX[g];
		const FVector LocalMuzzle(PortX, Side * GGunPortY, GGunPortZ);
		const FVector WorldMuzzle = HullTransform.TransformPosition(LocalMuzzle);

		// Out along the beam, trained towards the target within the traverse
		// the carriages allow, lifted to the elevation that drops the shot at
		// the target's range, then scattered a little so a broadside straddles.
		//
		// The lift is added as an explicit up component rather than a rotation
		// about the forward axis: that version put the shot 6 degrees BELOW the
		// horizon on both sides, and every ball splashed six metres out.
		FVector Beam = (GetActorRightVector() * Side).GetSafeNormal2D();
		float Elevation = GunElevationDeg;
		if (bLayingByHand && IsPlayerControlled())
		{
			// LAID BY HAND. The two locals the solver would have written are
			// written from the player's train and elevation instead, and every
			// line below - the scatter, the two random draws, the spawn, the
			// inherited way - is untouched. One function, one truth: a second
			// copy of FireBroadside for the player would have drifted from this
			// one the first time either was edited, and this project has paid
			// for exactly that before.
			Beam = Beam.RotateAngleAxis(LayRotationDeg(), FVector::UpVector);
			Elevation = LayElevationDeg;
		}
		else if (AimAt)
		{
			// Ordered high, the guns are laid on her main top; ordered low, on
			// her hull. Same formula, different mark.
			const AShipPawn* AimShip = Cast<AShipPawn>(AimAt);
			FVector AimPoint = (bHigh && AimShip)
				? AimShip->HighAimPoint() : AimAt->GetActorLocation();

			// Lead. Worked in the firing ship's frame, so the only velocity
			// that matters is the difference: two ships running side by side
			// at the same speed need no lead at all, and the relative form
			// says so without a special case.
			if (bLeadTarget)
			{
				const FVector TargetVel = AimAt->GetVelocity();
				// Against what the BALL actually carries, not against our own way.
				// With inheritance on the two are the same thing and the relative
				// form is right; with -ShipInheritVel=0 the ball leaves in the world
				// frame, the lead owes the whole of the target's motion, and taking
				// our own velocity off it aimed short by exactly that much times the
				// time of flight.
				const FVector RelVel = FVector(TargetVel.X - CarriedVel.X,
					TargetVel.Y - CarriedVel.Y, 0.f);
				const FVector Mark = AimPoint;
				for (int32 Pass = 0; Pass < FMath::Max(1, LeadPasses); ++Pass)
				{
					const float R = FVector::Dist2D(AimPoint, WorldMuzzle);
					// Horizontal speed is the muzzle speed to within a tenth of
					// a percent at the elevations these guns use (under six
					// degrees), so there is nothing to gain from the cosine.
					const float Flight = R / FMath::Max(1.f, MuzzleSpeed);
					AimPoint = Mark + RelVel * Flight;
				}
				LastLeadCm = FVector::Dist2D(AimPoint, Mark);
			}
			else
			{
				LastLeadCm = 0.f;
			}

			const FVector ToTarget = AimPoint - WorldMuzzle;
			const FVector Wanted = ToTarget.GetSafeNormal2D();
			// Signed angle from the beam to the target, clamped to traverse.
			const float Signed = FMath::RadiansToDegrees(FMath::Atan2(
				FVector::CrossProduct(Beam, Wanted).Z,
				FVector::DotProduct(Beam, Wanted)));
			const float Train = FMath::Clamp(Signed, -MaxTraverseDeg, MaxTraverseDeg);
			Beam = Beam.RotateAngleAxis(Train, FVector::UpVector);
			const float GroundRange = ToTarget.Size2D();
			// Elevation for the range, plus the angle the aim point subtends
			// above or below the muzzle. Without that second term the guns
			// ignored the fact that they stand 120 cm higher than the point
			// they are laid on, and the mean point of impact stood 238 cm up
			// the target's side, on her rail rather than her strake.
			// Firing high simply moves the aim point into her rig, so one
			// formula serves both orders.
			const float AimZ = ToTarget.Z;
			Elevation = ElevationForRangeDeg(GroundRange);
			if (GroundRange > 1.f)
			{
				Elevation += FMath::RadiansToDegrees(FMath::Atan2(AimZ, GroundRange));
			}
		}
		// WHAT THIS GUN WAS ACTUALLY LAID TO, recorded once, after every path has
		// had its say. The log line used to work the solver's answer out a second
		// time and printed 14.47 while the guns fired at two - a fact stated
		// twice drifts, and it is always the report that is wrong quietly.
		LaidElevationDeg = Elevation;
		// Scatter traverse and elevation separately: a symmetric cone lets the
		// elevation error dominate range far more than any gun crew would.
		const float TrainJitter = FMath::FRandRange(-SpreadDeg, SpreadDeg);
		const float ElevJitter = FMath::FRandRange(-ElevationSpreadDeg, ElevationSpreadDeg);
		Beam = Beam.RotateAngleAxis(TrainJitter, FVector::UpVector);
		const float Rise = FMath::Tan(FMath::DegreesToRadians(Elevation + ElevJitter));
		FVector Aim = (Beam + FVector::UpVector * Rise).GetSafeNormal();

		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.Instigator = this;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ACannonBall* Ball = GetWorld()->SpawnActor<ACannonBall>(
			CannonBallClass, WorldMuzzle, Aim.Rotation(), Params);
		if (!Ball)
		{
			continue;
		}
		// The ball carries the ship's way with it. Sampled before the broadside
		// so it is the ship's motion and not her own recoil.
		const FVector Inherited = CarriedVel;
		const int32 ThisShot = ShotCounter++;
		Ball->Fire(Aim * MuzzleSpeed + Inherited, PrimaryWaterBody, this,
			ThisShot, AimAt);

		// One puff per GUN, at that gun's muzzle, thrown along that gun's line.
		// Seeded from the shot index so the smoke draws its scatter from its own
		// stream: taking numbers off the global one would move every ball fired
		// afterwards, and -ShipSeed exists precisely so that cannot happen.
		if (bGunSmoke)
		{
			AGunSmoke::Spawn(GetWorld(), WorldMuzzle, Aim, ThisShot);
		}
		++Fired;

		if (HullCollision)
		{
			HullCollision->AddImpulseAtLocation(-Aim * RecoilImpulse, WorldMuzzle);
		}
	}

	if (Fired == 0)
	{
		// Every gun on this side is dismounted. Do not burn a reload on a
		// broadside that never happened.
		return false;
	}

	// Spent AFTER the loop, by exactly the number that went off, so the draw
	// sequence above never depends on the magazine.
	if (HasMagazine())
	{
		Shot = FMath::Max(0, Shot - Fired);
	}
	ShotFired += Fired;
	bReportedDry = false;

	Reload = ReloadSeconds;
	// Enough state to explain a range bias between two ships firing the same
	// gun: heel, own motion, and how high the first muzzle actually sits.
	const FVector FirstMuzzle = HullTransform.TransformPosition(
		FVector(GGunPortsX[0], Side * GGunPortY, GGunPortZ));
	const FVector Vel = VelBeforeRecoil;
	UE_LOG(LogTemp, Display,
		TEXT("SHOTLOG broadside %s side=%s guns=%d target=%s range=%.0fm heel=%.1f muzzleZ=%.0f velBeam=%.2f velFwd=%.2f elev=%.2f aim=%s lead=%.1fm inherit=%d bias=%.3f t=%.1f"),
		*GetName(), bStarboard ? TEXT("starboard") : TEXT("port"), Fired,
		AimAt ? *AimAt->GetName() : TEXT("none"),
		AimAt ? FVector::Dist2D(AimAt->GetActorLocation(), GetActorLocation()) * 0.01f : 0.f,
		GetActorRotation().Roll, FirstMuzzle.Z,
		FVector::DotProduct(Vel, GetActorRightVector() * Side) * 0.01f,
		FVector::DotProduct(Vel, GetActorForwardVector()) * 0.01f,
		// WHAT THE GUNS WERE LAID TO, not the solver's answer recomputed here. This
		// field used to do the latter, and with the guns laid by hand at two
		// degrees it printed 14.47 - the solver's answer for a target a kilometre
		// off - while the balls fell where two degrees puts them. A fact stated
		// twice, once as the action and once as the report, drifts apart, and it
		// is always the report that is wrong without anyone noticing.
		LaidElevationDeg,
		bHigh ? TEXT("high") : TEXT("low"),
		LastLeadCm * 0.01f, bInheritShipVelocity ? 1 : 0, RangeBias, GetWorld()->GetTimeSeconds());
	return Fired > 0;
}

float AShipPawn::GetHullTopZ() const
{
	return HullCollision
		? HullCollision->Bounds.Origin.Z + HullCollision->Bounds.BoxExtent.Z
		: GetActorLocation().Z;
}

float AShipPawn::GetRigEfficiency() const
{
	return ForeRigShare * ForeRigIntegrity + (1.f - ForeRigShare) * MainRigIntegrity;
}

int32 AShipPawn::GetGunsRemaining(bool bStarboard) const
{
	int32 Count = 0;
	for (int32 g = 0; g < 4; ++g)
	{
		Count += bGunDown[bStarboard ? 1 : 0][g] ? 0 : 1;
	}
	return Count;
}

EShipZone AShipPawn::ClassifyHit(const UPrimitiveComponent* Struck,
	const FVector& HullLocal, int32& OutGun) const
{
	OutGun = INDEX_NONE;

	// A scuttle is always a hole in the hull, whatever the synthetic hit
	// point happens to coincide with.
	if (bScuttling)
	{
		return EShipZone::Hull;
	}

	// The rig is identified by WHICH COMPONENT the shot struck, never by
	// where it landed: a low shot that strays above the box top without
	// touching a rig volume is honest hull damage, not a phantom in the tops.
	if (Struck && Struck == ForeRig)
	{
		return EShipZone::ForeRig;
	}
	if (Struck && Struck == MainRig)
	{
		return EShipZone::MainRig;
	}

	// Right aft AND near the centreline: the ball came in through the
	// transom. A shot on the beam passes the rudder without touching it,
	// which is why raking a ship from astern was worth the manoeuvre.
	if (HullLocal.X <= -1350.f && FMath::Abs(HullLocal.Y) <= 180.f &&
		HullLocal.Z >= -270.f && HullLocal.Z <= 90.f)
	{
		return EShipZone::Rudder;
	}

	// Through a gun port: out on the beam, at gun deck height, abreast of one
	// of the four ports.
	if (FMath::Abs(HullLocal.Y) >= 380.f &&
		HullLocal.Z >= GunDeckLowCm && HullLocal.Z <= GunDeckHighCm)
	{
		for (int32 g = 0; g < UE_ARRAY_COUNT(GGunPortsX); ++g)
		{
			if (FMath::Abs(HullLocal.X - GGunPortsX[g]) <= GunPortWindowCm)
			{
				OutGun = g;
				return EShipZone::Guns;
			}
		}
	}

	return EShipZone::Hull;
}

static const TCHAR* ZoneName(EShipZone Zone)
{
	switch (Zone)
	{
	case EShipZone::ForeRig: return TEXT("forerig");
	case EShipZone::MainRig: return TEXT("mainrig");
	case EShipZone::Rudder:  return TEXT("rudder");
	case EShipZone::Guns:    return TEXT("guns");
	default:                 return TEXT("hull");
	}
}

float AShipPawn::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent,
		EventInstigator, DamageCauser);

	if (IsSinking())
	{
		// The rest of the killing broadside lands a frame or two later.
		// Logged so the shot accounting stays honest, ignored otherwise.
		UE_LOG(LogTemp, Display, TEXT("SHOTLOG damage ignored, %s is sinking by=%s"),
			*GetName(), DamageCauser ? *DamageCauser->GetName() : TEXT("none"));
		return 0.f;
	}

	// Remember where the hit struck: the killing one decides which side floods.
	const UPrimitiveComponent* Struck = nullptr;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& Point = static_cast<const FPointDamageEvent&>(DamageEvent);
		LastHitLocal = GetActorTransform().InverseTransformPosition(Point.HitInfo.ImpactPoint);
		bHasLastHit = true;
		Struck = Point.HitInfo.Component.Get();
	}

	int32 Gun = INDEX_NONE;
	const EShipZone Zone = ClassifyHit(Struck, LastHitLocal, Gun);
	float HullLoss = 0.f;

	// Men, as well as timber. A grounding blow kills nobody: the ground does
	// not throw splinters, and bGroundingBlow already tags the log for it.
	const bool bShot = !bGroundingBlow;
	switch (Zone)
	{
	case EShipZone::ForeRig:
		ForeRigIntegrity = FMath::Max(0.f, ForeRigIntegrity - RigHitDamage);
		if (bShot) LoseHands(RigHitCasualties, TEXT("rig"));
		break;
	case EShipZone::MainRig:
		MainRigIntegrity = FMath::Max(0.f, MainRigIntegrity - RigHitDamage);
		if (bShot) LoseHands(RigHitCasualties, TEXT("rig"));
		break;
	case EShipZone::Rudder:
		RudderIntegrity = FMath::Max(0.f, RudderIntegrity - RudderHitDamage);
		if (bShot) LoseHands(RudderHitCasualties, TEXT("rudder"));
		break;
	case EShipZone::Guns:
	{
		const int32 SideIndex = (LastHitLocal.Y >= 0.f) ? 1 : 0;
		if (Gun >= 0 && !bGunDown[SideIndex][Gun])
		{
			bGunDown[SideIndex][Gun] = true;
			if (bShot) LoseHands(GunHitCasualties, TEXT("gun"));
		}
		else
		{
			// The port was already empty, so the ball went on into the hull.
			HullLoss = DamageAmount;
			if (bShot) LoseHands(HullHitCasualties, TEXT("hull"));
		}
		break;
	}
	default:
		HullLoss = DamageAmount;
		if (bShot) LoseHands(HullHitCasualties, TEXT("hull"));
		break;
	}

	HullIntegrity = FMath::Max(0.f, HullIntegrity - HullLoss);
	// The old line, kept word for word: it is the running reconciliation
	// between shots landed and hull lost, and reading taken=0 is exactly how
	// you see how much of a fight went aloft.
	// Tagged by cause. A wound from the ground used to print SHOTLOG, so it
	// was counted among the gunnery - including in the table that was supposed
	// to prove the island changed nothing but what it should.
	const TCHAR* Tag = bGroundingBlow ? TEXT("GROUNDLOG") : TEXT("SHOTLOG");
	UE_LOG(LogTemp, Display, TEXT("%s damage taken=%.0f integrity=%.0f/%.0f"),
		Tag, HullLoss, HullIntegrity, MaxHullIntegrity);
	UE_LOG(LogTemp, Display,
		TEXT("%s zone %s target=%s at=(%.0f,%.0f,%.0f) gun=%d rig=%.2f/%.2f rud=%.2f guns=%d/%d"),
		Tag,
		ZoneName(Zone), *GetName(), LastHitLocal.X, LastHitLocal.Y, LastHitLocal.Z,
		Gun, ForeRigIntegrity, MainRigIntegrity, RudderIntegrity,
		GetGunsRemaining(false), GetGunsRemaining(true));

	// A merchant does not fight to the last plank. On the same branch that
	// decides a sinking, and BELOW the zero test in the order of events: a
	// blow that carries her straight through the strike line and under the
	// waterline sinks her and never strikes her.
	if (HullIntegrity <= 0.f)
	{
		BeginSinking(DamageCauser);
	}
	else if (Allegiance == EShipAllegiance::Merchant && !bStruck
		&& (HullIntegrity <= StrikeBelowFraction * MaxHullIntegrity
			|| GetRigEfficiency() <= StrikeBelowRig))
	{
		Strike(DamageCauser);
	}
	return Applied;
}

void AShipPawn::Strike(AActor* Causer)
{
	// bMadePort as well as bStruck. MakePort() has always refused to run on a
	// ship that had struck; the reverse guard was missing, so a merchant who
	// was already safe under the fort could still haul down her colours to a
	// shot fired before she got there - and be counted BOTH through and
	// stopped, from one hull, which can decide the mission on its own.
	if (bStruck || bMadePort || IsSinking())
	{
		return;
	}
	bStruck = true;
	// The same words as a sinking: whatever the helm was doing stops
	// mattering. The controller reads HasStruck() and keeps her furled from
	// here on, because trim input is a RATE and a single order would be
	// overwritten by its next tick.
	SailTrimInput = -1.f;
	SteerInput = 0.f;
	UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s STRUCK by=%s hull=%.0f/%.0f rig=%.2f t=%.1f"),
		*GetName(), Causer ? *Causer->GetName() : TEXT("none"),
		HullIntegrity, MaxHullIntegrity, GetRigEfficiency(),
		GetWorld()->GetTimeSeconds());
	OnShipStruck.Broadcast(this, Causer);
}

void AShipPawn::LoseHands(int32 Count, const TCHAR* Why)
{
	if (Count <= 0 || Hands <= 0)
	{
		return;
	}
	const int32 Lost = FMath::Min(Count, Hands);
	Hands -= Lost;
	Casualties += Lost;
	UE_LOG(LogTemp, Display, TEXT("CREWLOG %s lost %d hands (%s), %d/%d left"),
		*GetName(), Lost, Why, Hands, HandsMax);
}

void AShipPawn::SetRepairShare(float Share)
{
	const float Clamped = FMath::Clamp(Share, 0.f, 0.75f);
	if (FMath::IsNearlyEqual(Clamped, RepairShare, 0.001f))
	{
		return;
	}
	RepairShare = Clamped;
	if (GetHandsOnRepair() > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("CREWLOG %s %d hands to repair (share %.2f), %d at the guns"),
			*GetName(), GetHandsOnRepair(), RepairShare, GetHandsOnGuns());
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("CREWLOG %s all hands to the guns"), *GetName());
	}
}

void AShipPawn::TickRepairs(float DeltaSeconds)
{
	const int32 OnRepair = GetHandsOnRepair();
	if (OnRepair <= 0 || IsSinking())
	{
		return;
	}
	// The rudder first, because a ship that cannot steer cannot do anything
	// else; then whichever mast is worse. Nothing above the jury cap is
	// touched, and the hull is not repaired at sea in this slice.
	float* Target = nullptr;
	const TCHAR* Name = TEXT("");
	if (RudderIntegrity < JuryCap)
	{
		Target = &RudderIntegrity;
		Name = TEXT("rudder");
	}
	else if (ForeRigIntegrity < JuryCap && ForeRigIntegrity <= MainRigIntegrity)
	{
		Target = &ForeRigIntegrity;
		Name = TEXT("fore rig");
	}
	else if (MainRigIntegrity < JuryCap)
	{
		Target = &MainRigIntegrity;
		Name = TEXT("main rig");
	}
	if (!Target)
	{
		return;
	}
	const float Applied = FMath::Min(OnRepair * RepairPerHandPerSecond * DeltaSeconds,
		JuryCap - *Target);
	*Target += Applied;
	RepairedTotal += Applied;
	if (!bRepairsLogged)
	{
		bRepairsLogged = true;
		UE_LOG(LogTemp, Display, TEXT("CREWLOG %s repairs begun on the %s with %d hands, t=%.1f"),
			*GetName(), Name, OnRepair, GetWorld()->GetTimeSeconds());
	}
}

void AShipPawn::OnRepairPressed()
{
	// R cycles a quarter, a half, and back to every man at the guns.
	SetRepairShare(RepairShare < 0.2f ? 0.25f : (RepairShare < 0.45f ? 0.5f : 0.f));
}

bool AShipPawn::DetachPrizeCrew(int32 Count)
{
	if (Count <= 0 || Hands - Count < MinHandsAboard)
	{
		return false;
	}
	Hands -= Count;
	HandsInPrizes += Count;
	UE_LOG(LogTemp, Display,
		TEXT("CREWLOG %s sent %d hands away to a prize, %d/%d left aboard (%d away in all)"),
		*GetName(), Count, Hands, HandsMax, HandsInPrizes);
	return true;
}

bool AShipPawn::LoadShot(int32 Rounds)
{
	if (Rounds <= 0 || !HasMagazine() || Shot >= ShotMax)
	{
		return false;
	}
	Shot = FMath::Min(ShotMax, Shot + Rounds);
	return true;
}

bool AShipPawn::RecruitHand()
{
	if (Hands >= HandsMax || IsSinking())
	{
		return false;
	}
	++Hands;
	return true;
}

float AShipPawn::RepairHull(float Points)
{
	// The one thing the repair parties at sea are explicitly not allowed to
	// touch: TickRepairs does the rudder and the masts and leaves the hull
	// alone, because knotting and splicing is not shipwrighting. This is the
	// only place a hull comes back, and it costs money.
	if (Points <= 0.f || IsSinking())
	{
		return 0.f;
	}
	const float Put = FMath::Min(Points, MaxHullIntegrity - HullIntegrity);
	HullIntegrity += Put;
	return Put;
}

int32 AShipPawn::TakeBackPrizeCrew(int32 Count)
{
	if (Count <= 0)
	{
		return 0;
	}
	// A deck only holds so many. With the port selling replacements this could
	// overflow: buy twelve men while a prize crew is still at sea, then have
	// that crew come home, and the ship carried 72 of a complement of 60.
	// GetHandsShort no longer sells those berths, and this is the belt to that
	// pair of braces - said out loud, because men turned away at the gangway
	// are men who were paid for twice.
	const int32 Room = FMath::Max(0, HandsMax - Hands);
	const int32 Back = FMath::Min(Count, Room);
	if (Back < Count)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CREWLOG %s has no berth for %d of %d men home from a prize"),
			*GetName(), Count - Back, Count);
	}
	Hands += Back;
	HandsReturned += Back;
	return Back;
}

void AShipPawn::ManAsPrize(AShipPawn* Taker, int32 CrewAboard)
{
	if (bIsPrize)
	{
		return;
	}
	bIsPrize = true;
	PrizeCrewAboard = CrewAboard;
	TakenBy = Taker;
	// Her own people are prisoners; the men who work her now are the ones who
	// came across. She keeps lying to, as she has since she struck.
	Hands = CrewAboard;
	SailTrimInput = -1.f;
	SteerInput = 0.f;
	UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s MANNED by=%s crew=%d t=%.1f"),
		*GetName(), Taker ? *Taker->GetName() : TEXT("none"), CrewAboard,
		GetWorld()->GetTimeSeconds());
}

bool AShipPawn::LandPrize(int32& OutReturned)
{
	OutReturned = 0;
	if (!bIsPrize || bPrizeLanded)
	{
		return false;
	}
	bPrizeLanded = true;
	// THE MEN STILL ALIVE, not the men who went across. PrizeCrewAboard is a
	// second copy of the number that boarded and is never decremented, while
	// shot taken on the run home comes off Hands - so returning PrizeCrewAboard
	// counted a casualty on the prize AND put the same man back on the
	// captor's deck. Hands is the roster that LoseHands actually maintains.
	const int32 Home = FMath::Min(PrizeCrewAboard, Hands);
	PrizeCrewAboard = 0;
	Hands = 0;
	SailTrimInput = -1.f;
	SteerInput = 0.f;

	// Captor, not Owner: AActor already has an Owner and a local of that name
	// hides it, which this project compiles as an error on purpose.
	AShipPawn* Captor = TakenBy.Get();
	if (Captor && !Captor->IsSunk())
	{
		OutReturned = Captor->TakeBackPrizeCrew(Home);
		UE_LOG(LogTemp, Display,
			TEXT("CREWLOG %s has %d hands home from a prize, %d/%d aboard (%d still away)"),
			*Captor->GetName(), OutReturned, Captor->Hands, Captor->HandsMax,
			Captor->GetHandsAway());
	}
	else
	{
		// Said out loud rather than quietly dropped: the men reached port,
		// the ship that sent them did not, and nobody is the better for it.
		UE_LOG(LogTemp, Warning,
			TEXT("CREWLOG %d hands off %s have no ship to return to"),
			Home, *GetName());
	}
	UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s LANDED crew=%d returned=%d t=%.1f"),
		*GetName(), Home, OutReturned, GetWorld()->GetTimeSeconds());
	return true;
}

void AShipPawn::MakePort()
{
	if (bMadePort || bStruck || IsSinking())
	{
		return;
	}
	bMadePort = true;
	SailTrimInput = -1.f;
	SteerInput = 0.f;
	UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s MADE PORT hull=%.0f/%.0f t=%.1f"),
		*GetName(), HullIntegrity, MaxHullIntegrity, GetWorld()->GetTimeSeconds());
}

void AShipPawn::ScuttleHull(bool bStarboard, AActor* Causer)
{
	FVector Local = ScuttleBreachLocal;
	Local.Y = FMath::Abs(Local.Y) * (bStarboard ? 1.f : -1.f);

	FHitResult Hit;
	Hit.ImpactPoint = GetActorTransform().TransformPosition(Local);
	Hit.Location = Hit.ImpactPoint;
	Hit.ImpactNormal = -GetActorRightVector() * (bStarboard ? 1.f : -1.f);
	Hit.Normal = Hit.ImpactNormal;

	// One blow worth the whole hull, delivered like a cannonball would be.
	FPointDamageEvent Event(HullIntegrity + 1.f, Hit, FVector::ZeroVector, nullptr);
	bScuttling = true;
	TakeDamage(Event.Damage, Event, nullptr, Causer);
	bScuttling = false;
}

void AShipPawn::Scuttle()
{
	ScuttleHull(true, this);
}

void AShipPawn::BeginSinking(AActor* Causer)
{
	if (SinkPhase != ESinkPhase::Afloat || !Buoyancy || !HullCollision)
	{
		return;
	}
	SinkPhase = ESinkPhase::Flooding;
	SinkTime = 0.f;
	PhaseTime = 0.f;
	FloodScale = 1.f;
	PlungeScale = 1.f;

	BreachLocal = bHasLastHit ? LastHitLocal : ScuttleBreachLocal;
	BreachLocal.X = FMath::Clamp(BreachLocal.X, -1550.f, 1550.f);
	BreachLocal.Y = FMath::Clamp(BreachLocal.Y, -520.f, 520.f);
	RestDraught = FMath::Min(GetActorLocation().Z - WaterSurfaceZ(), 0.f);

	// The two pontoons nearest the breach are holed, the rest merely leak.
	const TArray<FSphericalPontoon>& Pontoons = Buoyancy->BuoyancyData.Pontoons;
	const int32 Num = FMath::Min(Pontoons.Num(), (int32)UE_ARRAY_COUNT(GPontoons));
	int32 Nearest = -1, Second = -1;
	float NearestD = TNumericLimits<float>::Max(), SecondD = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Num; ++i)
	{
		RestCoefficient[i] = Pontoons[i].PontoonCoefficient;
		BreachWeight[i] = LeakingWeight;
		const float D = FVector2D::Distance(FVector2D(GPontoons[i].X, GPontoons[i].Y),
			FVector2D(BreachLocal.X, BreachLocal.Y));
		if (D < NearestD)
		{
			Second = Nearest; SecondD = NearestD;
			Nearest = i; NearestD = D;
		}
		else if (D < SecondD)
		{
			Second = i; SecondD = D;
		}
	}
	if (Nearest >= 0) BreachWeight[Nearest] = HoledWeight;
	if (Second >= 0) BreachWeight[Second] = HoledWeight;

	// Whatever the helm was doing stops mattering.
	SailTrimInput = 0.f;
	SteerInput = 0.f;
	HullCollision->WakeAllRigidBodies();

	UE_LOG(LogTemp, Display,
		TEXT("SHIPLOG %s SUNK by=%s breach=(%.0f,%.0f) side=%s holed=%d,%d t=%.1f draught=%.0f heel=%.1f lift=%.2f"),
		*GetName(), Causer ? *Causer->GetName() : TEXT("none"),
		BreachLocal.X, BreachLocal.Y, BreachLocal.Y >= 0.f ? TEXT("starboard") : TEXT("port"),
		Nearest, Second, PlayTime, RestDraught, GetActorRotation().Roll, LiftFraction);

	// Last statement on purpose: handlers only log, count and start timers.
	OnShipSunk.Broadcast(this, Causer);
}

static const TCHAR* SinkPhaseName(ESinkPhase Phase)
{
	switch (Phase)
	{
	case ESinkPhase::Flooding: return TEXT("flooding");
	case ESinkPhase::Foundering: return TEXT("foundering");
	case ESinkPhase::Plunging: return TEXT("plunging");
	case ESinkPhase::Wreck: return TEXT("wreck");
	default: return TEXT("afloat");
	}
}

bool AShipPawn::TickSinking(float DeltaSeconds)
{
	SinkTime += DeltaSeconds;
	PhaseTime += DeltaSeconds;
	const float Draught = GetActorLocation().Z - WaterSurfaceZ();
	const float U = FMath::Clamp(SinkTime / FloodSeconds, 0.f, 1.f);

	// Flood water: grows to its full weight over FloodSeconds, then holds.
	// Applied low and on the breach side, so it both trims and lists her.
	const FVector FloodCentreLocal(BreachLocal.X,
		FMath::Sign(BreachLocal.Y) * FloodCentreY, FloodCentreZ);
	HullCollision->AddForceAtLocation(
		FVector(0.f, 0.f, -U * FloodWaterFraction * ShipMassKg * 980.f),
		GetActorTransform().TransformPosition(FloodCentreLocal));

	switch (SinkPhase)
	{
	case ESinkPhase::Flooding:
	{
		// Closed loop on draught: take lift away only as fast as the hull
		// needs to follow the profile from rest to awash. Riding high means
		// open the holes wider; already deeper than the profile means let the
		// water in at a trickle and let her catch up.
		const float TargetDraught = FMath::Lerp(RestDraught, AwashDraughtCm, U);
		const float Lag = Draught - TargetDraught;
		const float Rate = (Lag > 0.f)
			? FMath::Min(FloodRateMax, FloodRateBase + Lag * FloodRateGain)
			: FloodTrickleRate;
		FloodScale = FMath::Max(0.f, FloodScale - Rate * DeltaSeconds);
		if (Draught <= AwashDraughtCm)
		{
			EnterFoundering(TEXT("draught"));
		}
		else if (SinkTime > FloodSeconds + AwashFailsafeSeconds)
		{
			EnterFoundering(TEXT("failsafe"));
		}
		break;
	}
	case ESinkPhase::Foundering:
		if (PhaseTime >= HangSeconds)
		{
			EnterPlunging();
		}
		break;
	case ESinkPhase::Plunging:
		PlungeScale = FMath::Max(0.f, PlungeScale - DeltaSeconds / PlungeSeconds);
		if (Draught <= WreckDepthCm || PhaseTime > PlungeFailsafeSeconds)
		{
			SinkPhase = ESinkPhase::Wreck;
			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s wrecked t=%.1f sinkT=%.1f z=%.0f reason=%s"),
				*GetName(), PlayTime, SinkTime, GetActorLocation().Z,
				Draught <= WreckDepthCm ? TEXT("depth") : TEXT("failsafe"));
			OnShipWrecked.Broadcast(this);
			Destroy();
			return true;
		}
		break;
	default:
		break;
	}
	ApplyLiftScales();
	return false;
}

void AShipPawn::EnterFoundering(const TCHAR* Reason)
{
	SinkPhase = ESinkPhase::Foundering;
	PhaseTime = 0.f;
	// A wreck at the surface must not be rammed by a fresh hull or stop a
	// shot: filter-only changes, the body is not rebuilt.
	HullCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	HullCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	// And her rigging with her, or a shot still finds the masts of a ship
	// that is already going down and the log reports a rig hit immediately
	// followed by the damage being ignored.
	for (UBoxComponent* Volume : { ForeRig.Get(), MainRig.Get() })
	{
		if (Volume)
		{
			Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	const FRotator Rot = GetActorRotation();
	UE_LOG(LogTemp, Display,
		TEXT("SHIPLOG %s awash t=%.1f sinkT=%.1f z=%.0f heel=%.1f pitch=%.1f lift=%.2f reason=%s"),
		*GetName(), PlayTime, SinkTime, GetActorLocation().Z, Rot.Roll, Rot.Pitch,
		LiftFraction, Reason);
}

void AShipPawn::EnterPlunging()
{
	SinkPhase = ESinkPhase::Plunging;
	PhaseTime = 0.f;
	HullCollision->SetLinearDamping(SinkLinearDamping);
	UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s plunging t=%.1f sinkT=%.1f z=%.0f"),
		*GetName(), PlayTime, SinkTime, GetActorLocation().Z);
}

void AShipPawn::ApplyLiftScales()
{
	// PontoonCoefficient multiplies the force AFTER the engine's clamp and is
	// only rewritten by the engine when a pontoon is enabled or disabled,
	// which never happens here. So it is a clean, linear lift lever.
	TArray<FSphericalPontoon>& Pontoons = Buoyancy->BuoyancyData.Pontoons;
	const int32 Num = FMath::Min(Pontoons.Num(), 6);
	for (int32 i = 0; i < Num; ++i)
	{
		const float Scale = FMath::Clamp(
			(1.f - (1.f - FloodScale) * BreachWeight[i]) * PlungeScale, 0.f, 1.f);
		Pontoons[i].PontoonCoefficient = RestCoefficient[i] * Scale;
	}
}

void AShipPawn::ApplyGroundContact()
{
	// Deliberately NOT guarded on being in a water body. This is the one force
	// that has to keep acting in exactly the condition that switches off the
	// keel, the sail drive and the rudder together, and a hull that has lost
	// all three is the hull that most needs pushing back out to sea.
	if (!HullCollision || IsSinking())
	{
		// A ship on her way down stops being aground, and stops telling anyone
		// which way the sea is. Without this the last reading before she
		// struck stays published for ever: her captain would go on being told
		// she is on the ground, with a direction taken from wherever she
		// happened to be, for the whole of the sinking.
		bAground = false;
		GroundOffshore = FVector::ZeroVector;
		GroundClosingMS = 0.f;
		PenetrationCm = 0.f;
		return;
	}

	const FVector Com = HullCollision->GetCenterOfMass();

	// Three points along her, not one. A thirty-one metre ship sampled at her
	// middle treats a fifty-metre bank as a thirty-four metre bank, and the
	// missing sixteen metres are exactly where her bow is. Measured before
	// this: held against the bank under full sail she settled at a centre
	// depth of 3708 - comfortably inside a 5000 margin by that reading - with
	// her stem two and a half metres past the shoreline.
	const FVector FwdH = GetActorForwardVector().GetSafeNormal2D();
	const FVector Probes[3] = {
		Com,
		Com + FwdH * GroundProbeAlongCm,
		Com - FwdH * GroundProbeAlongCm,
	};

	FVector Offshore = FVector::ZeroVector;
	float Deepest = 0.f;
	const AIsland* Nearest = nullptr;

	for (TActorIterator<AIsland> It(GetWorld()); It; ++It)
	{
		for (const FVector& Probe : Probes)
		{
			FVector Out;
			const float D = It->ShoalPenetrationAt(Probe, Out);
			if (D > Deepest)
			{
				Deepest = D;
				Offshore = Out;
				Nearest = *It;
			}
		}
	}

	PenetrationCm = Deepest;
	// Published for the captain, and HELD while she is aground rather than
	// cleared the instant the reading touches zero. Measured with it cleared:
	// a hull working off a bank crosses zero penetration over and over with
	// the swell, so the captain's claw-off state flickered on and off 21 times
	// in one run - two conditions with different hysteresis, ANDed. The
	// direction is cleared where the ship herself declares she is afloat.

	GroundClosingMS = 0.f;

	// How fast she is standing further in. Hoisted out of the force block
	// because the WOUND is owed to this, not to how fast she is travelling: a
	// ship running fast along a coast and just clipping the bank was being
	// charged for the whole of her speed, most of which was parallel to it.
	float Closing = 0.f;
	if (Deepest > 0.f)
	{
		Closing = FMath::Max(0.f, -FVector::DotProduct(
			HullCollision->GetPhysicsLinearVelocity(), Offshore));
		GroundClosingMS = Closing / 100.f;
		GroundOffshore = Offshore;
	}

	// Deep water takes no branch and adds no force at all. Open sea is left
	// alone by construction rather than by a small number.
	if (Deepest > 0.f)
	{
		++GroundForceTicks;
		// The clear-clock restarts only when she is properly on again, at the
		// same threshold that puts her aground. Resetting it for ANY reading
		// above zero meant the clock could never run: she comes to rest one or
		// two centimetres in, which is above zero every tick, so the release
		// window never opened and she stayed reported aground for ever.
		DeepestPenetrationCm = FMath::Max(DeepestPenetrationCm, Deepest);
		WorstPenetrationCm = FMath::Max(WorstPenetrationCm, Deepest);

		// Closing speed, counted only while she is still standing further in.
		// On the way out this term is zero, so the bank never resists her
		// leaving: it only ever takes way off her going on.
		// Strictly horizontal, and applied at the centre of mass, so it makes
		// no roll or pitch moment and leaves the draught and the heel to the
		// buoyancy and to HeelTorque, which were calibrated for them.
		const float DragRamp = FMath::Min(1.f, Deepest / FMath::Max(1.f, ShoalDragRampCm));

		// The bank stiffens sharply towards the beach - fifty times its outer
		// stiffness by the last hundred centimetres, and then flat at the
		// ceiling. Not infinitely: DIn is clamped and the ceiling is real, so
		// this is a bank that is strong ENOUGH, measured, and not one that is
		// impossible to cross by construction. Said plainly because the first
		// version of this comment claimed the latter, which would have been a
		// guarantee the code does not make.
		const float Margin = FMath::Max(1.f, Nearest ? Nearest->GetShoalMarginCm() : 1.f);
		const float DIn = FMath::Min(Deepest, Margin * 0.98f);
		const float Stiffen = Margin / FMath::Max(1.f, Margin - DIn);

		const float Weight = HullCollision->GetMass() * 980.f;
		float Magnitude = ShoalPushK * DIn * Stiffen + ShoalDragK * DragRamp * Closing;
		const float Ceiling = MaxGroundForceWeights * Weight;
		if (Magnitude > Ceiling)
		{
			Magnitude = Ceiling;
			if (!bWarnedForceCapped)
			{
				bWarnedForceCapped = true;
				UE_LOG(LogTemp, Warning,
					TEXT("SHIPLOG %s ground force hit its ceiling at depth=%.0f of margin=%.0f"),
					*GetName(), Deepest, Margin);
			}
		}

		FVector Push = Offshore * Magnitude;
		Push.Z = 0.f;
		HullCollision->AddForceAtLocation(Push, Com);

		if (Deepest > ShoalBiteCm)
		{
			ClearSince = 0.f;
		}

		if (Nearest && Deepest >= Nearest->GetShoalMarginCm() && !bWarnedOnRock)
		{
			bWarnedOnRock = true;
			UE_LOG(LogTemp, Warning,
				TEXT("SHIPLOG %s REACHED THE ROCK depth=%.0f margin=%.0f - the bank is too weak"),
				*GetName(), Deepest, Nearest->GetShoalMarginCm());
		}
	}

	const float Speed = HullCollision->GetPhysicsLinearVelocity().Size2D() / 100.f;

	// Aground means properly on, not brushing the line: without the threshold
	// a hull lying at the edge took a fresh wound every time the swell carried
	// her a centimetre in.
	if (!bAground && Deepest > ShoalBiteCm)
	{
		bAground = true;
		AgroundSince = PlayTime;
		AgroundEntrySpeed = Closing / 100.f;
		UE_LOG(LogTemp, Display,
			TEXT("SHIPLOG %s AGROUND on %s closing=%.2f speed=%.2f depth=%.0f margin=%.0f pos=(%.0f,%.0f)"),
			*GetName(), Nearest ? *Nearest->GetName() : TEXT("?"),
			AgroundEntrySpeed, Speed, Deepest,
			Nearest ? Nearest->GetShoalMarginCm() : 0.f,
			GetActorLocation().X, GetActorLocation().Y);

		// One blow, on the rising edge only, delivered down the real damage
		// path so the flooding, the list and the sinking behave exactly as
		// they do for round shot. Charged per m/s she was making: touching
		// gently is nearly free, driving her on is not.
		//
		// X = 900 is well clear of the rudder window (X <= -1350) and |Y| = 300
		// is well inside the gun ports (which need |Y| >= 380), so ClassifyHit
		// cannot return anything but Hull. Off the centreline on purpose: the
		// two pontoons nearest the middle are one per side, and a centreline
		// breach would founder her dead level through a flooding case that has
		// never been calibrated.
		// Which side she bilges on is the side the ground is on, not a coin:
		// deterministic, so a grounding can be measured twice and compared,
		// and truer besides - she takes it where the bank is.
		FVector Local(900.f, 300.f, -300.f);
		const FVector Inshore = -Offshore;
		Local.Y *= (FVector::DotProduct(Inshore, GetActorRightVector()) >= 0.f)
			? 1.f : -1.f;

		FHitResult Hit;
		Hit.ImpactPoint = GetActorTransform().TransformPosition(Local);
		Hit.Location = Hit.ImpactPoint;
		Hit.ImpactNormal = FVector::UpVector;
		Hit.Normal = Hit.ImpactNormal;

		const float Blow = GroundingDamagePerMS * AgroundEntrySpeed;
		FPointDamageEvent Event(Blow, Hit, FVector::ZeroVector, nullptr);
		// Flagged so the damage lines say GROUNDLOG rather than SHOTLOG. A
		// wound from the ground was being counted in the gunnery totals - in
		// this slice's own measurement table, among other places.
		bGroundingBlow = true;
		TakeDamage(Event.Damage, Event, nullptr,
			Nearest ? const_cast<AIsland*>(Nearest) : Cast<AActor>(this));
		bGroundingBlow = false;
	}
	else if (bAground && Deepest <= ShoalBiteCm)
	{
		// Off once she has been under the bite for a few seconds. The two
		// thresholds are the same number on purpose: she is AGROUND above
		// 50 cm and AFLOAT below it, which is a proper band rather than two
		// unrelated tests. Measured with a test for exact zero instead: the
		// spring pushes her out exponentially and she settles at one or two
		// centimetres, rocking across zero with the swell, so "clear for three
		// seconds" never once came true and she lay there reported aground,
		// 1 cm into a 5000 cm bank, for the rest of the run.
		if (ClearSince <= 0.f)
		{
			ClearSince = PlayTime;
		}
		if (PlayTime - ClearSince >= ShoalReleaseSeconds)
		{
			bAground = false;
			GroundOffshore = FVector::ZeroVector;
			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s AFLOAT after=%.1fs entrySpeed=%.2f exitSpeed=%.2f deepest=%.0f hull=%.0f"),
				*GetName(), PlayTime - AgroundSince, AgroundEntrySpeed, Speed,
				DeepestPenetrationCm, HullIntegrity);
			DeepestPenetrationCm = 0.f;
			ClearSince = 0.f;
		}
	}
}

FVector AShipPawn::HighAimPoint() const
{
	return GetActorLocation()
		+ GetActorForwardVector().GetSafeNormal2D() * HighAimAlongCm
		+ FVector::UpVector * HighAimHeightCm;
}

void AShipPawn::ApplyLateralResistance()
{
	// A keel works whenever water flows past it, which is not the same as
	// whenever the sails draw: this deliberately sits outside the drive block,
	// or a ship caught in irons would have no lateral plane at all and would
	// be blown bodily sideways.
	if (!HullCollision || IsSinking() || !Buoyancy || !Buoyancy->IsInWaterBody())
	{
		return;
	}

	// Horizontal hull axes. NOT GetActorRightVector(): heeled over, that
	// vector carries a vertical component, and a side force of several
	// million would then be pumped up and down at the roll period, moving the
	// very draught the buoyancy was calibrated at.
	const FVector FwdH = GetActorForwardVector().GetSafeNormal2D();
	if (FwdH.IsNearlyZero())
	{
		return;
	}
	const FVector RightH = FVector::CrossProduct(FVector::UpVector, FwdH);

	const FVector ComWorld = HullCollision->GetCenterOfMass();
	const FVector VelCom = HullCollision->GetPhysicsLinearVelocity();
	const FVector Omega = HullCollision->GetPhysicsAngularVelocityInRadians();
	const float Surge = FVector::DotProduct(VelCom, FwdH);

	// Two panels, each carrying half the plane, one radius of gyration fore
	// and aft of the centre of lateral resistance. Their centroid is that
	// centre, so steady sideslip makes the right yaw moment; their spacing is
	// the plane's own gyradius, so a yaw rate makes the right yaw damping.
	// Both sit at the height of the centre of mass, so the plane adds no roll
	// moment of its own and leaves the heel to HeelTorque, which is calibrated.
	const float PanelX[2] = {
		LateralCentreXCm + LateralGyradiusCm,
		LateralCentreXCm - LateralGyradiusCm };

	for (int32 i = 0; i < 2; ++i)
	{
		const FVector PanelWorld = ComWorld + FwdH * PanelX[i];
		const FVector PanelVel = VelCom + FVector::CrossProduct(Omega, FwdH * PanelX[i]);
		const float Sway = FVector::DotProduct(PanelVel, RightH);
		const float Flow = FMath::Sqrt(Surge * Surge + Sway * Sway);

		const float Side = -(PanelLiftK * Flow * Sway
			+ PanelCrossK * FMath::Abs(Sway) * Sway);

		HullCollision->AddForceAtLocation(RightH * Side, PanelWorld);
	}
}

float AShipPawn::ComputeLift()
{
	float Sum = 0.f;
	if (Buoyancy)
	{
		for (const FSphericalPontoon& Pontoon : Buoyancy->BuoyancyData.Pontoons)
		{
			Sum += Pontoon.LocalForce.Z;
		}
	}
	LiftFraction = Sum / (ShipMassKg * 980.f);
	return LiftFraction;
}

float AShipPawn::WaterSurfaceZ() const
{
	if (!Buoyancy)
	{
		return 0.f;
	}
	float Sum = 0.f;
	int32 Wet = 0;
	for (const FSphericalPontoon& Pontoon : Buoyancy->BuoyancyData.Pontoons)
	{
		if (Pontoon.bIsInWater)
		{
			Sum += Pontoon.WaterHeight;
			++Wet;
		}
	}
	return Wet > 0 ? Sum / Wet : 0.f;
}

void AShipPawn::OnHullHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// One line per second per hull: the cannonballs already log their own hits.
	if (Cast<ACannonBall>(OtherActor) || PlayTime - LastHullHitLogTime < 1.f)
	{
		return;
	}
	LastHullHitLogTime = PlayTime;
	UE_LOG(LogTemp, Display,
		TEXT("SHIPLOG %s CONTACT with=%s comp=%s normalZ=%.2f impulse=%.2e at z=%.0f"),
		*GetName(), OtherActor ? *OtherActor->GetName() : TEXT("?"),
		OtherComp ? *OtherComp->GetName() : TEXT("?"),
		Hit.ImpactNormal.Z, NormalImpulse.Size(), Hit.ImpactPoint.Z);
}

void AShipPawn::OnElevate(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	bLayingByHand = true;
	LayElevationDeg = FMath::Clamp(LayElevationDeg + Value * ElevationPerNotchDeg,
		MinLayElevationDeg, MaxLayElevationDeg);
}

void AShipPawn::OnLayLockPressed()
{
	// Pegged dead abeam, and it is a TOGGLE rather than a hold: the whole point
	// is that the player can stop steering the guns and go back to steering the
	// ship, and a key he has to keep a finger on gives him neither hand back.
	bLayingByHand = true;
	bLayLocked = !bLayLocked;
	if (bLayLocked)
	{
		LayTrainDeg = 0.f;
		bAgainstStop = false;
	}
}

void AShipPawn::UpdateGunLaying()
{
	if (!IsPlayerControlled())
	{
		return;
	}

	// Pinned from the command line: no mouse in a headless run, and a visual
	// feature that cannot be captured is one that cannot be reviewed.
	if (!bLayPinned)
	{
		float PinTrain = 0.f, PinElev = 0.f;
		const bool bHaveTrain =
			FParse::Value(FCommandLine::Get(), TEXT("LayTrain="), PinTrain);
		const bool bHaveElev =
			FParse::Value(FCommandLine::Get(), TEXT("LayElev="), PinElev);
		int32 PinSide = 1;
		FParse::Value(FCommandLine::Get(), TEXT("LayStarboard="), PinSide);
		if (bHaveTrain || bHaveElev)
		{
			bLayPinned = true;
			bLayingByHand = true;
			bLayStarboard = PinSide != 0;
			if (bHaveElev)
			{
				LayElevationDeg = FMath::Clamp(PinElev, MinLayElevationDeg, MaxLayElevationDeg);
			}
			// Clamped exactly as the mouse would be, and the stop flag set the
			// same way, so a pinned run exercises the real path and not a
			// parallel one that could drift from it.
			bAgainstStop = FMath::Abs(PinTrain) > MaxTraverseDeg + 0.01f;
			LayTrainDeg = FMath::Clamp(PinTrain, -MaxTraverseDeg, MaxTraverseDeg);
			UE_LOG(LogTemp, Display,
				TEXT("AIMLOG pinned: side=%s train=%+.1f (asked %+.1f) elev=%.1f stop=%d"),
				bLayStarboard ? TEXT("starboard") : TEXT("port"),
				LayTrainDeg, PinTrain, LayElevationDeg, bAgainstStop ? 1 : 0);
		}
	}
	if (bLayPinned)
	{
		AAimIndicator::For(this);
		return;
	}

	// Where the player is looking, relative to the bow. The camera boom runs on
	// the controller's yaw (bUsePawnControlRotation), and the hull does not, so
	// this difference is exactly the mouse's contribution and nothing else.
	const AController* C = GetController();
	if (!C)
	{
		return;
	}
	const float LookRel = FMath::UnwindDegrees(
		C->GetControlRotation().Yaw - GetActorRotation().Yaw);

	// Which battery the player is looking at. Taken from the sign alone, so the
	// guns change sides the instant the eye crosses the bow or the stern rather
	// than at some hysteresis band the player cannot see.
	const bool bWantStarboard = LookRel >= 0.f;
	const float Beam = bWantStarboard ? 90.f : -90.f;
	// Off the beam, positive FORWARD on both sides, so one number reads the same
	// whichever battery is laid and the picture does not have to know the side.
	const float Wanted = FMath::UnwindDegrees(LookRel - Beam) * (bWantStarboard ? -1.f : 1.f);

	if (bLayLocked)
	{
		bLayStarboard = bWantStarboard;
		LayTrainDeg = 0.f;
		bAgainstStop = false;
		AAimIndicator::For(this);
		return;
	}

	// bLayingByHand is set by the INPUT HANDLERS - the mouse axis, the wheel, the
	// lock key - and never inferred from where the camera happens to point. See
	// OnTurnCamera for what inferring it cost.
	if (!bLayingByHand)
	{
		return;
	}

	bLayStarboard = bWantStarboard;
	LayTrainDeg = FMath::Clamp(Wanted, -MaxTraverseDeg, MaxTraverseDeg);
	// The picture follows the guns, and it makes itself the first time they are
	// laid: nothing has to be placed in the level, because this project builds
	// its levels from script and a hand-placed actor is one that will be missing
	// from somebody's map.
	AAimIndicator::For(this);
	// HARD AGAINST THE STOP. This is the one piece of state the whole feature
	// turns on: the guns stop following the mouse, visibly, and the only way to
	// get them further round is the helm. Nothing says so in words anywhere -
	// the player sees the barrels refuse and works it out, which is the way a
	// rule is actually learned.
	bAgainstStop = FMath::Abs(Wanted) > MaxTraverseDeg + 0.01f;
}

float AShipPawn::GetGunPortHeightCm() const
{
	// One door out of the anonymous namespace, so the fall-of-shot dial and the
	// guns cannot disagree about how high the guns are. They already disagreed
	// once about something simpler than this.
	return GGunPortZ;
}

float AShipPawn::LayForwardDot() const
{
	const float Side = bLayStarboard ? 1.f : -1.f;
	const FVector Beam = (GetActorRightVector() * Side).GetSafeNormal2D();
	const FVector Laid = Beam.RotateAngleAxis(LayRotationDeg(), FVector::UpVector);
	return (float)FVector::DotProduct(Laid, GetActorForwardVector().GetSafeNormal2D());
}

float AShipPawn::RangeForElevationCm(float ElevationDeg) const
{
	// FROM THE HEIGHT THE GUNS ACTUALLY STAND AT, and the first version was the
	// flat-ground formula. Measured against where the balls really fell, it read
	// 153 m where they landed at 228, 307 where they landed at 355, 458 against
	// 490, 608 against 625 - wrong by half at the flattest elevation and closing
	// as the barrels rose. That shape is the signature of muzzle height, and the
	// guns had been raised to 2.97 m above the sea the day before: the bar was a
	// dial calibrated for a ship that no longer existed.
	//
	// A shot leaving at height h still has to fall that extra h before it stops,
	// so it flies on past the flat-ground answer, and the surplus is largest when
	// the trajectory is flattest. Hence
	//
	//     R = (V cos0 / g) * ( V sin0 + sqrt( (V sin0)^2 + 2 g h ) )
	//
	// which collapses to the old V^2 sin(20) / g exactly when h is zero.
	const float Theta = FMath::DegreesToRadians(FMath::Clamp(ElevationDeg, 0.05f, 89.f));
	const float V = MuzzleVelocityMS * 100.f;
	const float G = FMath::Abs(GetWorld() ? GetWorld()->GetGravityZ() : -980.f);
	if (G < 1.f)
	{
		return 0.f;
	}
	// The height the muzzles ride above the LOCAL sea, not above zero: she rises
	// and falls on the swell, and a dial that ignored that would be right only
	// between waves.
	const float Height = FMath::Max(0.f,
		GetActorLocation().Z + GetGunPortHeightCm() - WaterSurfaceZ());
	const float VS = V * FMath::Sin(Theta);
	const float VC = V * FMath::Cos(Theta);
	const float Reach = (VC / G) * (VS + FMath::Sqrt(VS * VS + 2.f * G * Height));
	return Reach / FMath::Max(0.01f, RangeBias);
}

void AShipPawn::OnFirePort()
{
	// Laid by hand, the guns go where they are pointed and nowhere else. Passing
	// a target here would let the solver quietly re-aim them, which is precisely
	// the magic this feature exists to take away: the player who lines up his
	// own shot and misses has to be allowed to miss.
	FireBroadside(false, bLayingByHand ? nullptr : FindTargetOnSide(false), bAimHigh);
}

void AShipPawn::OnFireStarboard()
{
	FireBroadside(true, bLayingByHand ? nullptr : FindTargetOnSide(true), bAimHigh);
}

void AShipPawn::Tick(float DeltaSeconds)
{
	// The guns follow the eye before anything else this frame: a broadside fired
	// on this tick must go where the barrels are NOW, not where they were when
	// the mouse was last read.
	UpdateGunLaying();

	// The wind, into the rig. Masts bend a little, sails breathe, cordage moves
	// most - each by its own SwayAmount, all from the one wind the sea and the
	// sails already use, so nothing can disagree about which way it blows.
	if (HullMesh && RigMaterials.Num() == 0)
	{
		const int32 Slots = HullMesh->GetNumMaterials();
		for (int32 i = 0; i < Slots; ++i)
		{
			if (UMaterialInstanceDynamic* M = HullMesh->CreateAndSetMaterialInstanceDynamic(i))
			{
				RigMaterials.Add(M);
			}
		}
	}
	if (RigMaterials.Num() > 0)
	{
		float WindMS = 0.f, BearingDeg = 0.f;
		if (const UWindSubsystem* W = GetWorld()->GetSubsystem<UWindSubsystem>())
		{
			WindMS = W->GetWindSpeedMS();
			BearingDeg = W->GetWindBearingDeg();
		}
		// Blowing TOWARDS, which is the way a shroud is pushed.
		const float Rad = FMath::DegreesToRadians(BearingDeg + 180.f);
		const float Now = GetWorld()->GetTimeSeconds();
		for (UMaterialInstanceDynamic* M : RigMaterials)
		{
			M->SetScalarParameterValue(TEXT("WindVecX"), FMath::Cos(Rad));
			M->SetScalarParameterValue(TEXT("WindVecY"), FMath::Sin(Rad));
			M->SetScalarParameterValue(TEXT("WindSpeedMS"), WindMS);
			M->SetScalarParameterValue(TEXT("WindTime"), Now);
		}
		if (!bRigSwayReported)
		{
			bRigSwayReported = true;
			float Back = -1.f;
			const bool bTook = RigMaterials[0]->GetScalarParameterValue(
				TEXT("WindSpeedMS"), Back);
			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s rigsway mats=%d wind=%.1f m/s toward %.0f deg "
					 "(readback %s %.1f)"),
				*GetName(), RigMaterials.Num(), WindMS,
				FMath::Fmod(BearingDeg + 180.f, 360.f),
				bTook ? TEXT("ok") : TEXT("FAILED"), Back);
		}
	}

	Super::Tick(DeltaSeconds);

	if (!HullCollision || !HullCollision->IsSimulatingPhysics())
	{
		return;
	}

	PlayTime += DeltaSeconds;
	if (!HullCollision->RigidBodyIsAwake())
	{
		// Belt and braces for the sleep settings in the constructor.
		HullCollision->WakeAllRigidBodies();
	}
	// The guns reload as fast as the men left to serve them. With a full
	// crew and nobody sent to the carpenter this is exactly the old line.
	const float GunCrew = GetGunCrewFactor();
	PortReload = FMath::Max(0.f, PortReload - DeltaSeconds * GunCrew);
	StarboardReload = FMath::Max(0.f, StarboardReload - DeltaSeconds * GunCrew);
	TickRepairs(DeltaSeconds);
	ComputeLift();

	if (FireTestAt > 0.f && !bFireTestDone && PlayTime >= FireTestAt && !IsSinking())
	{
		bFireTestDone = true;
		FireBroadside(true, FindTargetOnSide(true));
	}

	UWindSubsystem* Wind = GetWind();

	if (IsSinking())
	{
		if (TickSinking(DeltaSeconds))
		{
			return;   // the wreck destroyed itself; touch nothing
		}
		// Sheets let fly: whatever sail was set comes down in a couple of seconds.
		SailTrim = FMath::Max(0.f, SailTrim - 2.f * TrimRate * DeltaSeconds);
	}
	else if (RunAgroundAt > 0.f && PlayTime >= RunAgroundAt)
	{
		// Drive her straight at the nearest island under full sail, then take
		// the sail off her ten seconds after she strikes. This exists because
		// a clean island run proves nothing: without a forced grounding, a
		// broken bank and a working one produce the same log.
		const AIsland* Nearest = nullptr;
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AIsland> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared2D(GetActorLocation(),
				It->GetActorLocation());
			if (D < Best)
			{
				Best = D;
				Nearest = *It;
			}
		}
		if (Nearest)
		{
			const FVector To = Nearest->GetActorLocation() - GetActorLocation();
			const float Err = FMath::FindDeltaAngleDegrees(
				GetActorRotation().Yaw, To.Rotation().Yaw);
			SteerInput = FMath::Clamp(Err / 20.f, -1.f, 1.f);
		}
		if (bAground && !bRunAgroundFurled && PlayTime - AgroundSince >= 10.f)
		{
			bRunAgroundFurled = true;
			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s run-aground test: taking in sail"), *GetName());
		}
		SailTrim = bRunAgroundFurled
			? FMath::Max(0.f, SailTrim - TrimRate * DeltaSeconds) : 1.f;
	}
	else if (RudderTestAt > 0.f && PlayTime >= RudderTestAt)
	{
		// Full sail, full helm: the steady yaw rate that comes out is what the
		// keel's yaw damping has to be paid for.
		SailTrim = 1.f;
		SteerInput = 1.f;
	}
	else if (bPolarTest && Wind)
	{
		// Full sail, and the helm held on one heading until she settles. The
		// wind is left exactly where -WindBearing put it: moving the wind AND
		// letting her head swing, which is what the old sweep did, means the
		// wind angle in a row changes for two reasons and the row belongs to
		// neither of them.
		SailTrim = 1.f;

		const float WindFrom = FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f);
		if (PolarRow == 0 && PolarDwellElapsed <= 0.f)
		{
			// Start hard on the wind and work away from it.
			PolarTargetYaw = FMath::UnwindDegrees(WindFrom + 30.f);
		}

		const float Err = FMath::FindDeltaAngleDegrees(
			GetActorRotation().Yaw, PolarTargetYaw);
		SteerInput = FMath::Clamp(Err / 25.f, -1.f, 1.f);

		PolarDwellElapsed += DeltaSeconds;
		PolarSettleTimer += DeltaSeconds;

		const float SpeedNow = HullCollision
			? HullCollision->GetPhysicsLinearVelocity().Size2D() / 100.f : 0.f;
		// THREE consecutive quiet seconds, not one. A ship still accelerating
		// passes a one-second test whenever her acceleration happens to be
		// small for a moment, and the first version let those rows through:
		// the table came out non-monotonic INSIDE a single pass, which no
		// steady state can be.
		if (PolarSettleTimer >= 1.f)
		{
			const bool bQuiet = PolarLastSpeed >= 0.f
				&& FMath::Abs(SpeedNow - PolarLastSpeed) < PolarSettledMS
				&& FMath::Abs(Err) < 2.f;
			PolarQuietSeconds = bQuiet ? PolarQuietSeconds + 1 : 0;
			PolarLastSpeed = SpeedNow;
			PolarSettleTimer = 0.f;
		}
		const bool bSettled = PolarQuietSeconds >= 3;

		// Give her at least a few seconds before believing she has settled:
		// a ship that has not begun to accelerate is momentarily as steady as
		// one that has finished.
		// Twelve seconds, not six: at six most rows were reporting themselves
		// settled at exactly the floor, which means the floor was deciding and
		// not the ship.
		const bool bLongEnough = PolarDwellElapsed > 15.f;
		// The row is WRITTEN further down, where drive, leeway, track and VMG
		// have all been recomputed for this frame. Reading them here would
		// report last frame's numbers next to this frame's speed.
		if ((bSettled && bLongEnough) || PolarDwellElapsed >= PolarDwellSeconds)
		{
			bPolarRowDue = true;
			bPolarRowSettled = bSettled && bLongEnough;
		}
	}
	else if (Wind && WindSweepSeconds > 0.f)
	{
		// Diagnostic sweep: walk the wind around the compass at a steady rate.
		Wind->SetForcedBearing(
			FMath::Fmod(PlayTime / WindSweepSeconds * 360.f, 360.f));
		SailTrim = 1.f;
	}
	else
	{
		SailTrim = FMath::Clamp(
			SailTrim + SailTrimInput * TrimRate * DeltaSeconds, 0.f, 1.f);
	}

	const FVector Forward = GetActorForwardVector();
	// The rig's side force and the keel's both act in the horizontal plane.
	// Using the heeled right vector for the rig was injecting sin(heel) of the
	// side force straight up, about two percent of the ship's weight, with the
	// sign flipping every time she changed tack.
	const FVector Right = FVector::CrossProduct(
		FVector::UpVector, Forward.GetSafeNormal2D());
	const FVector Velocity = HullCollision->GetPhysicsLinearVelocity();
	const float ForwardSpeed = FVector::DotProduct(Velocity, Forward);

	const bool bInWater = Buoyancy && Buoyancy->IsInWaterBody();

	ApplyLateralResistance();
	ApplyGroundContact();

	// Where she actually goes, as opposed to where she points. Below a knot
	// the track direction is noise, so leeway is reported as zero there.
	const FVector Track(Velocity.X, Velocity.Y, 0.f);
	const FVector Heading = Forward.GetSafeNormal2D();
	LeewayDeg = 0.f;
	if (Track.SizeSquared() > 50.f * 50.f)
	{
		const FVector TrackDir = Track.GetSafeNormal();
		LeewayDeg = FMath::RadiansToDegrees(FMath::Atan2(
			FVector::CrossProduct(Heading, TrackDir).Z,
			FVector::DotProduct(Heading, TrackDir)));
	}

	float Drive = 0.f;

	if (Wind)
	{
		// The wind angle is measured against where the wind comes FROM, so a
		// value near zero means the bow points straight into it.
		const FVector WindFrom = -Wind->GetWindDirection();
		const float Dot = FMath::Clamp(
			FVector::DotProduct(Forward.GetSafeNormal2D(), WindFrom), -1.f, 1.f);
		WindAngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));

		const float WindFactor = FMath::Clamp(
			Wind->GetWindSpeedMS() / FMath::Max(1.f, ReferenceWindMS), 0.f, 1.6f);
		// Canvas that has been shot away drives nothing.
		Drive = SailDriveCoefficient(WindAngleDeg) * SailTrim * WindFactor
			* GetRigEfficiency();

		// Ground made good straight to windward: the only number that says
		// whether a ship can beat at all. A fine heading with enough leeway
		// makes this zero or negative while she still shows six knots.
		WindwardVMG = FVector::DotProduct(Track, WindFrom) * 0.01f;

		// The angle the TRACK makes with the wind, measured the same way the
		// heading is. Subtracting leeway from the wind angle looked equivalent
		// and was not: the wind angle is an unsigned arccosine and leeway is a
		// signed arctangent, so the subtraction was only right on one tack.
		TrackWindAngleDeg = WindAngleDeg;
		if (Track.SizeSquared() > 50.f * 50.f)
		{
			TrackWindAngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Track.GetSafeNormal(), WindFrom), -1.f, 1.f)));
		}
	}
	else
	{
		WindwardVMG = 0.f;
		TrackWindAngleDeg = 0.f;
	}
	if (IsSinking())
	{
		Drive = 0.f;   // a rig on a foundering hull drives nothing
	}
	LastDrive = Drive;

	if (!IsSinking() && bInWater && Wind && Drive > KINDA_SMALL_NUMBER)
	{
		const float SpeedFraction =
			FMath::Clamp(ForwardSpeed / MaxForwardSpeed, 0.f, 1.f);
		const float Falloff = 1.f - SpeedFraction;
		HullCollision->AddForce(Forward * MaxSailForce * Drive * Falloff);

		// Which side the wind presses on decides leeway and which way she heels.
		const float LateralWind =
			FVector::DotProduct(Wind->GetWindDirection(), Right);

		HullCollision->AddForce(
			Right * MaxSailForce * Drive * LeewayFraction * LateralWind);

		HullCollision->AddTorqueInRadians(
			Forward * HeelTorque * Drive * LateralWind);
	}

	// Two axes reach the helm and both are honoured: the keys and the arrows.
	const float Helm = FMath::Clamp(SteerInput + SteerRateInput, -1.f, 1.f);

	// The rudder needs water flowing past it, so steering scales with speed.
	if (!IsSinking() && bInWater && !FMath::IsNearlyZero(Helm))
	{
		float Authority = FMath::Clamp(
			FMath::Abs(ForwardSpeed) / FullSteeringSpeed, 0.f, 1.f);

		// With sail set but no way on, the crew backs the yards to pay off.
		// Without this a ship that stalls head to wind is stuck for good: no
		// speed means no rudder, and no rudder means it never leaves the wind.
		if (SailTrim > 0.1f)
		{
			Authority = FMath::Max(Authority, BackedSailAuthority);
		}

		// A rudder only reverses under real sternway. The old threshold of
		// 20 cm/s flipped it on the slightest drift, so a ship backing her sails
		// to pay off turned the wrong way and pinned herself head to wind.
		const float Direction = (ForwardSpeed < -100.f) ? -1.f : 1.f;
		// A shot-away rudder does not leave her helpless, only nearly so: a
		// crew can still coax her round by bracing the yards.
		const float Steering = FMath::Max(RudderIntegrity, RudderlessAuthority);
		HullCollision->AddTorqueInRadians(
			FVector(0.f, 0.f, Helm * TurnTorque * Authority * Direction * Steering));
	}

	// The gallery. Counted, not assumed: every frame writes a line saying which
	// file it asked for, so an empty Screenshots folder can be told apart from
	// a capture that was never requested.
	for (int32 i = 0; i < ShotTimes.Num(); ++i)
	{
		if (!ShotTaken[i] && PlayTime >= ShotTimes[i])
		{
			ShotTaken[i] = true;
			// TENTHS, not whole seconds. -ShipShots=10.6,11.4 both rounded to
			// "t011" and the second capture silently overwrote the first, so a
			// gallery meant to show a thing changing showed one frame twice.
			const FString File = FString::Printf(TEXT("%s_t%04d"),
				*ShotName, FMath::RoundToInt(ShotTimes[i] * 10.f));
			FScreenshotRequest::RequestScreenshot(File, false, false);
			UE_LOG(LogTemp, Display,
				TEXT("SHOTCAM wrote %s at t=%.1f (cam=%s)"),
				*File, PlayTime, ShotCam.IsEmpty() ? TEXT("free") : *ShotCam);
		}
	}

	if (ShotAfterSeconds > 0.f && !bShotTaken && PlayTime >= ShotAfterSeconds)
	{
		bShotTaken = true;
		// GEngine->Exec("HighResShot") logs nothing and writes no file in a
		// -game run. The engine's own request API does both.
		FScreenshotRequest::RequestScreenshot(TEXT("ShipFloat"), false, false);
		UE_LOG(LogTemp, Display, TEXT("SHIPLOG screenshot requested at t=%.1fs"),
			PlayTime);
	}

	// One-shot dump of what the buoyancy component actually thinks, because
	// "inWater=1" turned out to be a flag we set ourselves, not a measurement.
	if (bLogFloatState && Buoyancy && PlayTime >= 3.f && !bBuoyancyDumped)
	{
		bBuoyancyDumped = true;
		const FBuoyancyData& D = Buoyancy->BuoyancyData;
		int32 Wet = 0;
		float FirstDepth = -1.f;
		float SumForceZ = 0.f;
		for (int32 i = 0; i < D.Pontoons.Num(); ++i)
		{
			if (D.Pontoons[i].bIsInWater)
			{
				++Wet;
			}
			if (i == 0)
			{
				FirstDepth = D.Pontoons[i].ImmersionDepth;
			}
			SumForceZ += D.Pontoons[i].LocalForce.Z;
			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s pontoon[%d] r=%.0f coef=%.3f immersion=%.0f waterZ=%.0f fz=%.3e"),
				*GetName(), i, D.Pontoons[i].Radius, D.Pontoons[i].PontoonCoefficient,
				D.Pontoons[i].ImmersionDepth, D.Pontoons[i].WaterHeight, D.Pontoons[i].LocalForce.Z);
		}
		// What buoyancy actually carries, as a fraction of the weight. Afloat
		// on buoyancy alone this reads ~1.0; if it reads well under 1 something
		// else is holding the hull up.
		UE_LOG(LogTemp, Display, TEXT("SHIPLOG %s lift=%.3f (sumFz=%.3e weight=%.3e) async=%d"),
			*GetName(), SumForceZ / (HullCollision->GetMass() * 980.f), SumForceZ,
			HullCollision->GetMass() * 980.f, Buoyancy->IsUsingAsyncPath() ? 1 : 0);
		UE_LOG(LogTemp, Display,
			TEXT("SHIPLOG %s buoyancy active=%d overlapping=%d inBody=%d wetPontoons=%d/%d depth0=%.0f sim=%s"),
			*GetName(), Buoyancy->IsActive() ? 1 : 0,
			Buoyancy->IsOverlappingWaterBody() ? 1 : 0,
			Buoyancy->IsInWaterBody() ? 1 : 0,
			Wet, D.Pontoons.Num(), FirstDepth,
			Buoyancy->GetSimulatingComponent()
				? *Buoyancy->GetSimulatingComponent()->GetName() : TEXT("NONE"));
	}

	if (bPolarRowDue && Wind)
	{
		bPolarRowDue = false;
		UE_LOG(LogTemp, Display,
			TEXT("POLARLOG %s row=%d windAng=%.0f drive=%.3f speed=%.2f leeway=%.1f track=%.0f vmg=%.2f heel=%.1f held=%.0fs settled=%d"),
			*GetName(), PolarRow, WindAngleDeg, LastDrive,
			HullCollision ? HullCollision->GetPhysicsLinearVelocity().Size2D() / 100.f : 0.f,
			LeewayDeg, TrackWindAngleDeg, WindwardVMG,
			GetActorRotation().Roll, PolarDwellElapsed, bPolarRowSettled ? 1 : 0);

		++PolarRow;
		PolarDwellElapsed = 0.f;
		PolarLastSpeed = -1.f;
		PolarSettleTimer = 0.f;
		PolarTargetYaw = FMath::UnwindDegrees(PolarTargetYaw + PolarStepDeg);
	}

	// The polar diagram, one row per two degrees of wind rotation, carrying
	// the three numbers that decide whether she can beat: what the rig gives,
	// how fast she goes, and how much of that is thrown away as leeway.
	if (WindSweepSeconds > 0.f && Wind)
	{
		SweepLogTimer += DeltaSeconds;
		if (SweepLogTimer >= WindSweepSeconds / 180.f)
		{
			SweepLogTimer = 0.f;
			UE_LOG(LogTemp, Display,
				TEXT("SWEEPLOG %s windAng=%.0f drive=%.2f speed=%.2f leeway=%.1f track=%.0f vmg=%.2f heel=%.1f"),
				*GetName(), WindAngleDeg, Drive, ForwardSpeed * 0.01f, LeewayDeg,
				TrackWindAngleDeg, WindwardVMG, GetActorRotation().Roll);
		}
	}

	if (bLogFloatState)
	{
		LogTimer += DeltaSeconds;
		if (LogTimer >= 1.f)
		{
			const FRotator Rot = GetActorRotation();
			// Over the interval that actually elapsed, which is a second plus
			// one frame, and not at all on the first sample: there is no
			// previous heading to difference against, only a zero.
			YawRateDegPerSec = bHasLastYaw
				? FMath::FindDeltaAngleDegrees(LastYaw, Rot.Yaw) / LogTimer : 0.f;
			LastYaw = Rot.Yaw;
			bHasLastYaw = true;
			LogTimer = 0.f;

			UE_LOG(LogTemp, Display,
				TEXT("SHIPLOG %s t=%.0f windAng=%.0f windMS=%.1f trim=%.2f drive=%.2f speed=%.2fm/s heel=%.1f z=%.1f inWater=%d hull=%.0f reload=%.0f/%.0f lift=%.2f vz=%.1f awake=%d pitch=%.1f sink=%s sinkT=%.0f leeway=%.1f vmg=%.2f yaw=%.2f rig=%.2f/%.2f rud=%.2f guns=%d/%d pos=(%.0f,%.0f) yawDeg=%.0f shoal=%.0f agr=%d"),
				*GetName(), PlayTime, WindAngleDeg,
				Wind ? Wind->GetWindSpeedMS() : 0.f,
				SailTrim, Drive, ForwardSpeed * 0.01f, Rot.Roll,
				GetActorLocation().Z, bInWater ? 1 : 0,
				HullIntegrity, PortReload, StarboardReload, LiftFraction, Velocity.Z,
				HullCollision->RigidBodyIsAwake() ? 1 : 0, Rot.Pitch,
				SinkPhaseName(SinkPhase), SinkTime, LeewayDeg, WindwardVMG,
				YawRateDegPerSec, ForeRigIntegrity, MainRigIntegrity, RudderIntegrity,
				GetGunsRemaining(false), GetGunsRemaining(true),
				GetActorLocation().X, GetActorLocation().Y, Rot.Yaw,
				PenetrationCm, bAground ? 1 : 0);

			if (IsSinking() && Buoyancy)
			{
				// The calibration instrument for the sinking numbers: what each
				// pontoon is allowed to carry and what it actually carries.
				const TArray<FSphericalPontoon>& Pontoons = Buoyancy->BuoyancyData.Pontoons;
				FString Scales, Forces;
				int32 Wet = 0;
				const float W = ShipMassKg * 980.f;
				for (int32 i = 0; i < Pontoons.Num(); ++i)
				{
					const float Rest = (i < 6 && RestCoefficient[i] > 0.f) ? RestCoefficient[i] : 1.f;
					Scales += FString::Printf(TEXT("%s%.2f"), i ? TEXT(",") : TEXT(""),
						Pontoons[i].PontoonCoefficient / Rest);
					Forces += FString::Printf(TEXT("%s%.2f"), i ? TEXT(",") : TEXT(""),
						Pontoons[i].LocalForce.Z / W);
					Wet += Pontoons[i].bIsInWater ? 1 : 0;
				}
				UE_LOG(LogTemp, Display,
					TEXT("SINKLOG %s t=%.0f sinkT=%.1f phase=%s draught=%.0f surf=%.0f vz=%.2f heel=%.1f pitch=%.1f lift=%.2f flood=%.2f plunge=%.2f ls=%s fz=%s wet=%d/%d"),
					*GetName(), PlayTime, SinkTime, SinkPhaseName(SinkPhase),
					GetActorLocation().Z - WaterSurfaceZ(), WaterSurfaceZ(),
					Velocity.Z * 0.01f, Rot.Roll, Rot.Pitch, LiftFraction,
					FloodScale, PlungeScale, *Scales, *Forces, Wet, Pontoons.Num());
			}
		}
	}
}

void AShipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!PlayerInputComponent)
	{
		return;
	}

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AShipPawn::OnSailTrimInput);
	// Two separate handlers. Bound to the same one, whichever ran last won,
	// and since the arrows are bound after the keys they overwrote A and D
	// with zero every frame: the keys did not steer the ship at all.
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AShipPawn::OnSteer);
	PlayerInputComponent->BindAxis(TEXT("TurnRate"), this, &AShipPawn::OnSteerRate);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AShipPawn::OnTurnCamera);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AShipPawn::OnLookUp);

	PlayerInputComponent->BindAction(TEXT("FirePort"), IE_Pressed,
		this, &AShipPawn::OnFirePort);
	PlayerInputComponent->BindAction(TEXT("FireStarboard"), IE_Pressed,
		this, &AShipPawn::OnFireStarboard);
	PlayerInputComponent->BindAction(TEXT("AimHigh"), IE_Pressed,
		this, &AShipPawn::OnAimHighPressed);
	PlayerInputComponent->BindAction(TEXT("AimHigh"), IE_Released,
		this, &AShipPawn::OnAimHighReleased);
	PlayerInputComponent->BindAction(TEXT("Repair"), IE_Pressed,
		this, &AShipPawn::OnRepairPressed);
	PlayerInputComponent->BindAxis(TEXT("Elevate"), this, &AShipPawn::OnElevate);
	PlayerInputComponent->BindAction(TEXT("LayAbeam"), IE_Pressed,
		this, &AShipPawn::OnLayLockPressed);
}

void AShipPawn::OnSailTrimInput(float Value)
{
	SetSailTrimInput(Value);
}

void AShipPawn::OnSteer(float Value)
{
	SetSteerInput(Value);
}

void AShipPawn::OnSteerRate(float Value)
{
	if (!IsSinking())
	{
		SteerRateInput = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void AShipPawn::OnAimHighPressed()
{
	bAimHigh = true;
}

void AShipPawn::OnAimHighReleased()
{
	bAimHigh = false;
}

void AShipPawn::OnTurnCamera(float Value)
{
	AddControllerYawInput(Value);
	// THE MOUSE ITSELF is the signal that the player has taken the guns, and the
	// first version inferred it from GEOMETRY instead - "the eye is more than a
	// degree off the beam, so he must be laying". That is true from the first
	// frame of any run with nobody at the keyboard, because the camera starts
	// wherever it starts: hand laying switched itself on in all twenty-six
	// measured scenarios, the -ShipFireTest broadside was fired at the carriage
	// stop instead of at the solver's answer, and `gunnery` moved struck 9 -> 8.
	// An axis handler only runs when an axis moved, so this cannot happen here.
	if (FMath::Abs(Value) > KINDA_SMALL_NUMBER)
	{
		bLayingByHand = true;
	}
}

void AShipPawn::OnLookUp(float Value)
{
	AddControllerPitchInput(Value);
}
