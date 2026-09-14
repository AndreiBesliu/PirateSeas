#include "CannonBall.h"

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
	Collision->SetSimulatePhysics(true);
	Collision->SetEnableGravity(true);
	Collision->SetNotifyRigidBodyCollision(true);
	Collision->SetMassOverrideInKg(NAME_None, ShotMassKg, true);
	// Round shot barely notices the air over the ranges we fire at.
	Collision->SetLinearDamping(0.02f);
	Collision->SetUseCCD(true);
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
	FlightTime += DeltaSeconds;

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

	// The sea has no collision the ball can hit, so compare against the
	// queried surface height instead.
	if (Water)
	{
		FVector SurfaceLoc, SurfaceNormal, WaterVel;
		float Depth = 0.f;
		// Waves included: the sea the hulls float on is the sea the shot
		// splashes into.
		if (Water->GetWaterSurfaceInfoAtLocation(GetActorLocation(), SurfaceLoc,
			SurfaceNormal, WaterVel, Depth, true))
		{
			if (GetActorLocation().Z <= SurfaceLoc.Z)
			{
				ReportAndDie(TEXT("splash"), GetActorLocation());
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
		UGameplayStatics::ApplyPointDamage(OtherActor, ImpactDamage,
			GetVelocity().GetSafeNormal(), Hit,
			IsValid(Shooter) ? Shooter->GetInstigatorController() : nullptr,
			this, nullptr);
		UE_LOG(LogTemp, Display, TEXT("SHOTLOG hit by=%s shot=%d target=%s damage=%.0f"),
			IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
			*OtherActor->GetName(), ImpactDamage);
	}

	ReportAndDie(TEXT("impact"), Hit.ImpactPoint);
}

void ACannonBall::ReportAndDie(const TCHAR* Reason, const FVector& Where)
{
	bSpent = true;

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
	float AlongM = 0.f, LateralM = 0.f;
	if (IsValid(Target))
	{
		const FVector Line = (Target->GetActorLocation() - LaunchLocation).GetSafeNormal2D();
		const FVector Miss = Where - Target->GetActorLocation();
		AlongM = FVector::DotProduct(Miss, Line) * 0.01f;
		LateralM = FVector::CrossProduct(Line, Miss).Z * 0.01f;
	}
	UE_LOG(LogTemp, Display,
		TEXT("SHOTLOG %s by=%s shot=%d range=%.0fm flight=%.2fs impactZ=%.0f along=%+.1f lateral=%+.1f"),
		Reason, IsValid(Shooter) ? *Shooter->GetName() : TEXT("?"), ShotIndex,
		RangeM, FlightTime, Where.Z, AlongM, LateralM);

	if (Collision)
	{
		Collision->SetSimulatePhysics(false);
		Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	SetLifeSpan(0.05f);
}
