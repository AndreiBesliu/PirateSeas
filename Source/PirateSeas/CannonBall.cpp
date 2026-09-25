#include "CannonBall.h"

#include "HullSplinters.h"
#include "ShotTrail.h"

#include "Island.h"
#include "ShipPawn.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "WaterBodyComponent.h"
#include "OceanSurface.h"

ACannonBall::ACannonBall()
{
	PrimaryActorTick.bCanEverTick = true;
	InitialLifeSpan = 14.f;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->InitSphereRadius(16.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_PhysicsBody);
	// Round shot only ever strikes a hull. The sea is not a solid target: its
	// collision box is a hundred metres deep and swallowed every shot four
	// metres from the muzzle. Splashes come from the surface query in Tick.
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// On the body rather than through the setters - see the same change in
	// AShipPawn's constructor for why: the setters recompute mass properties
	// and ask for a physical material before GEngine exists, and the cook
	// counts that as an error.
	Collision->BodyInstance.bSimulatePhysics = true;
	Collision->SetEnableGravity(true);
	Collision->SetNotifyRigidBodyCollision(true);
	Collision->BodyInstance.SetMassOverride(ShotMassKg, true);
	// Round shot barely notices the air over the ranges we fire at.
	Collision->SetLinearDamping(0.02f);
	// Continuous collision, set on the body for the same reason as the mass
	// above: a round shot crosses its own diameter in well under a tick.
	Collision->BodyInstance.bUseCCD = true;
	// A simulated body only reports overlaps if it is asked to.
	Collision->SetGenerateOverlapEvents(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.32f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereAsset.Succeeded())
	{
		Mesh->SetStaticMesh(SphereAsset.Object);
	}
}

void ACannonBall::BeginPlay()
{
	Super::BeginPlay();
	if (Collision)
	{
		Collision->OnComponentHit.AddDynamic(this, &ACannonBall::OnHit);
	{
		int32 Flag = 1;
		if (FParse::Value(FCommandLine::Get(), TEXT("ShipSplinters="), Flag))
		{
			bSplinters = Flag != 0;
		}
	}
	}
}

void ACannonBall::Fire(const FVector& Velocity, UWaterBodyComponent* InWater,
	AActor* InShooter, int32 InShotIndex, AActor* InTarget)
{
	Water = InWater;
	LastSweepFrom = GetActorLocation();
	bSwept = true;
	Shooter = InShooter;
	Target = InTarget;
	ShotIndex = InShotIndex;
	LaunchLocation = GetActorLocation();

	if (Collision)
	{
		// Note: this does NOT protect the shooter. MoveIgnoreActors is only
		// read by component sweeps, and this ball is moved by the solver. What
		// actually protects her is the muzzle sitting at 640 out on the beam,
		// clear of the hull box's 520, plus the shooter test in OnHit.
		if (Shooter)
		{
			Collision->IgnoreActorWhenMoving(Shooter, true);
		}

		Collision->SetPhysicsLinearVelocity(Velocity);
	}
}

void ACannonBall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bSpent)
	{
		return;
	}
	// THE WAY SHE WAS GOING, kept from the last tick before anything touched
	// her. OnHit fires AFTER the physics step that resolved the contact, so by
	// then GetVelocity() is the ball coming back OFF the hull - measured: every
	// hull hit in the suite logged a direction pointing out of the ship. Nothing
	// in the game consumed that direction, which is how it stayed wrong; the
	// first thing that did (a probe asking whether each ball's line would have
	// met timber) got an answer of 100% false.
	LastFlightVel = GetVelocity();
	FlightTime += DeltaSeconds;

	// The wisp she leaves. One line, and it knows nothing about the trail: the
	// actor finds or makes itself. Laid BEFORE the sweep and the water query
	// below, so a ball that dies this frame has already marked where it got to.
	AShotTrail::Lay(GetWorld(), GetActorLocation(), TrailChain);

	// Look for rigging along the path just covered. The rig volumes are
	// query-only, so they raise no contact and the ball must sweep for them.
	// A single position test would miss: at 150 m/s the ball moves 250 cm in a
	// sixtieth of a second, against a mast volume 460 cm thick.
	const FVector Now = GetActorLocation();
	if (bSwept && GetWorld() && !Now.Equals(LastSweepFrom, 1.f))
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CannonBallRig), false, this);
		if (IsValid(Shooter))
		{
			Params.AddIgnoredActor(Shooter);
		}
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Vehicle);
		// And land. Measured before this line existed: 4 of 45 balls crossed
		// 120 m of solid rock, because the ball IGNORES every channel but
		// ECC_Pawn. The fix goes here, in the sweep, rather than on the ball's
		// collision response: that all-channels-ignore line is the fix for the
		// ocean's collision box swallowing every shot four metres from the
		// muzzle, a response edit is global to everything the ball will ever
		// meet, and the sweep already solves the 250-cm-per-frame tunnelling
		// that a 150 m/s ball has anyway.
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);

		// MULTI, not single. This level already holds THREE other bodies whose
		// object type is WorldStatic - the water zone's 42 km mesh, the ocean
		// body, and the gameplay debugger's renderer - and a single-hit sweep
		// that finds one of them has to be abandoned, which means the ball
		// never looks past it. The first version returned out of Tick there,
		// so a ball flying inside the water zone's bounds would have been
		// stopped from ever finding a ship at all, every frame, in silence.
		TArray<FHitResult> Hits;
		const FHitResult* Found = nullptr;
		if (GetWorld()->SweepMultiByObjectType(Hits, LastSweepFrom, Now,
			FQuat::Identity, Objects, FCollisionShape::MakeSphere(16.f), Params))
		{
			for (const FHitResult& Hit : Hits)
			{
				AActor* A = Hit.GetActor();
				if (Cast<AShipPawn>(A) || Cast<AIsland>(A))
				{
					Found = &Hit;
					break;
				}
				if (!bWarnedStepOver)
				{
					bWarnedStepOver = true;
					UE_LOG(LogTemp, Warning,
						TEXT("SHOTLOG sweep steps over %s, which is neither ship nor land"),
						A ? *A->GetName() : TEXT("?"));
				}
			}
		}
		if (Found)
		{
			const FHitResult& RigHit = *Found;
			AActor* Struck = RigHit.GetActor();
			bSpent = true;
			// Land takes no damage and has no rig: a ball that finds rock just
			// stops there. Worth logging as its own kind of end, because a
			// squadron that wastes its broadsides into an island is land
			// working as cover, and that has to be visible as a number rather
			// than inferred from a fall in hits.
			if (Cast<AIsland>(Struck))
			{
				// ONE line, like every other way a ball can end. The first
				// version wrote its own line AND the terminal one, so land
				// counted double against splashes in the very table meant to
				// show what the island changed.
				ReportAndDie(TEXT("land"), RigHit.ImpactPoint);
				return;
			}
			if (Struck)
			{
				UGameplayStatics::ApplyPointDamage(Struck, ImpactDamage,
					GetVelocity().GetSafeNormal(), RigHit,
					IsValid(Shooter) ? Shooter->GetInstigatorController() : nullptr,
					this, nullptr);
				UE_LOG(LogTemp, Display,
					TEXT("SHOTLOG rig by=%s shot=%d target=%s comp=%s"),
					IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
					*Struck->GetName(),
					RigHit.Component.IsValid() ? *RigHit.Component->GetName() : TEXT("?"));
			}
			ReportAndDie(TEXT("rigged"), RigHit.ImpactPoint);
			return;
		}
	}
	LastSweepFrom = Now;

	// The sea has no collision the ball can hit, so compare against the queried
	// surface height instead.
	//
	// WITH THE WAVES. The call this used to make was
	// GetWaterSurfaceInfoAtLocation(..., true), and the comment above it said
	// "waves included" - but that `true` is bIncludeDepth, and that function
	// never asks for EWaterBodyQueryFlags::IncludeWaves at all. So the shot was
	// tested against the flat water PLANE while the hulls float on the Gerstner
	// surface, two sea levels half a metre apart: 25 splashes in the logs, not
	// one of them above Z=0, on a sea with a metre of wave height.
	if (Water)
	{
		const EWaterBodyQueryFlags Flags = EWaterBodyQueryFlags::ComputeLocation
			| EWaterBodyQueryFlags::IncludeWaves;
		const TValueOrError<FWaterBodyQueryResult, EWaterBodyQueryError> Query =
			Water->TryQueryWaterInfoClosestToWorldLocation(GetActorLocation(), Flags);
		if (Query.HasValue() && !Query.GetValue().IsInExclusionVolume())
		{
			SurfaceZAtDeath = Query.GetValue().GetWaterSurfaceLocation().Z;

			// A BALL CANNOT SPLASH BEFORE IT HAS FLOWN. The gun ports stand about
			// 120 cm over the hull origin, she floats some 70 cm deeper than the
			// waterline she was modelled at, and heel drops the lee battery
			// further still - so on a hard-heeled or bow-down frame the muzzle is
			// genuinely under the local surface, and without this the whole
			// broadside is destroyed on its first tick. It has happened: four
			// balls, one timestamp, range=0m flight=0.00s, four foam rings on her
			// own beam, a burned reload, and four entries in the splash count
			// that never touched the sea.
			//
			// Sized to the SHIP, not the barrel: at these elevations a ball
			// rises about a tenth of what it travels, so anything less than a
			// hull length buys centimetres. A descending ball is exempt - that is
			// a genuine short plunge, not a muzzle.
			const bool bDescending = GetVelocity().Z < 0.f;
			const float OutCm = FVector::Dist2D(GetActorLocation(), LaunchLocation);
			if (bDescending || OutCm > 1600.f)
			{
				if (GetActorLocation().Z <= SurfaceZAtDeath)
				{
					ReportAndDie(TEXT("splash"), GetActorLocation());
				}
			}
			else if (!bReportedAwash && GetActorLocation().Z <= SurfaceZAtDeath)
			{
				// Counted, once per ball. Silence here would put the number of
				// balls that started underwater at zero for ever.
				bReportedAwash = true;
				UE_LOG(LogTemp, Warning,
					TEXT("SHOTLOG awash by=%s shot=%d muzzleZ=%.0f surfZ=%.0f clear=%.0f"),
					IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
					GetActorLocation().Z, SurfaceZAtDeath,
					GetActorLocation().Z - SurfaceZAtDeath);
			}
		}
	}
}

void ACannonBall::LifeSpanExpired()
{
	// A ball that ran out of fuse ended in silence, so the endings in a log
	// never added up to the shots fired and a missing ball was invisible.
	if (!bSpent)
	{
		ReportAndDie(TEXT("fuse"), GetActorLocation());
	}
	Super::LifeSpanExpired();
}

void ACannonBall::OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bSpent || OtherActor == Shooter)
	{
		return;
	}
	// Spent before anything else, so a ball cannot be charged twice in one
	// frame by the rig sweep and the contact notify both.
	bSpent = true;

	if (OtherActor)
	{
		// The shooter may be a wreck, or gone, by the time the ball lands.
		const FVector Incoming = LastFlightVel.IsNearlyZero()
			? GetVelocity() : LastFlightVel;
		UGameplayStatics::ApplyPointDamage(OtherActor, ImpactDamage,
			Incoming.GetSafeNormal(), Hit,
			IsValid(Shooter) ? Shooter->GetInstigatorController() : nullptr,
			this, nullptr);
		UE_LOG(LogTemp, Display, TEXT("SHOTLOG hit by=%s shot=%d target=%s damage=%.0f"),
			IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
			*OtherActor->GetName(), ImpactDamage);

		// AND SOMETHING TO SEE. Until 19.09 a hull hit produced this log line and
		// a number on a bar, while a MISS threw up a column of water visible at
		// three hundred metres - the one outcome the player is trying for was the
		// one with nothing to look at.
		//
		// Ships only. Rock does not splinter, and a ball into a hillside already
		// ends with its own reason string; oak coming out of an island would be
		// the effect lying about what was hit.
		if (bSplinters && Cast<AShipPawn>(OtherActor))
		{
			// The struck ship's way, so the burst stays with the hole, and the
			// sea's height there, so the pieces end in the water rather than
			// tumbling through it. Both are read HERE because this is the only
			// place that knows which ship was hit.
			AHullSplinters::Spawn(GetWorld(), Hit.ImpactPoint,
				Hit.ImpactNormal, ShotIndex,
				FVector(OtherActor->GetVelocity().X, OtherActor->GetVelocity().Y, 0.f),
				SurfaceZAtDeath);
		}
	}

	ReportAndDie(TEXT("impact"), Hit.ImpactPoint);
}

void ACannonBall::ReportAndDie(const TCHAR* Reason, const FVector& Where)
{
	bSpent = true;

	// Close the ribbon WHERE SHE DIED. The last sample was taken up to three
	// metres back, and a trail that stops short of the splash fails at the one
	// question it exists to answer: where did the shot actually land.
	AShotTrail::Close(GetWorld(), TrailChain, Where);

	// Tell the sea, but only when the sea is what was hit. A ball that went
	// through a hull or into a hillside has no business throwing up water, and
	// the reason string is already the thing that knows which happened.
	if (FCString::Strcmp(Reason, TEXT("splash")) == 0)
	{
		AOceanSurface::ReportSplash(GetWorld(), Where);
	}

	const float RangeM = FVector::Dist2D(LaunchLocation, Where) * 0.01f;

	// Where it died relative to what it was aimed at: along the line of fire
	// (+ is long) and across it. A ball that is "long" by less than the hull's
	// half thickness with near-zero lateral error went straight through.
	// A ball with no target has no miss to report, and printing +0.0/+0.0 for it
	// is printing a DEAD-CENTRE HIT: the same characters a perfect shot makes.
	// A quarter of the endings in the gale scenario's log were that - shots
	// fired at nothing on that side, and shots whose target was destroyed in
	// flight. Anything that averaged this field averaged those in as bullseyes.
	bool bHaveMark = false;
	float AlongM = 0.f, LateralM = 0.f;
	if (IsValid(Target))
	{
		const FVector Line = (Target->GetActorLocation() - LaunchLocation).GetSafeNormal2D();
		const FVector Miss = Where - Target->GetActorLocation();
		AlongM = FVector::DotProduct(Miss, Line) * 0.01f;
		LateralM = FVector::CrossProduct(Line, Miss).Z * 0.01f;
		bHaveMark = true;
	}
	const FString Mark = bHaveMark
		? FString::Printf(TEXT("along=%+.1f lateral=%+.1f"), AlongM, LateralM)
		: FString(TEXT("along=nomark lateral=nomark"));
	// surfZ is the gate on the wave-aware query: it read ~0 on every shot ever
	// logged while the sea had a metre of wave in it, and it has to move with the
	// swell now. A run in which it is negative on all of them is the flat plane
	// back again.
	UE_LOG(LogTemp, Display,
		TEXT("SHOTLOG %s by=%s shot=%d range=%.0fm flight=%.2fs impactZ=%.0f surfZ=%.0f %s"),
		Reason, IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
		RangeM, FlightTime, Where.Z, SurfaceZAtDeath, *Mark);

	if (Collision)
	{
		Collision->SetSimulatePhysics(false);
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetLifeSpan(0.05f);
}
