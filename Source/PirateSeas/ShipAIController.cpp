#include "ShipAIController.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "EngineUtils.h"
#include "Island.h"
#include "SeaGameMode.h"
#include "ShipPawn.h"
#include "WindSubsystem.h"

AShipAIController::AShipAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	bAttachToPawn = false;
}

void AShipAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// -AIBeatMargin=N sets how far outside the no-go zone she lays a
	// close-hauled board. It exists so the number can be SWEPT rather than
	// argued about: the polar says her best windward gain is at 60 degrees,
	// but the polar is a steady state and a beating ship spends part of her
	// life turning, so what a margin is worth has to be measured on a whole
	// engagement, not read off a curve.
	float Margin = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("AIBeatMargin="), Margin) && Margin >= 0.f)
	{
		CloseHauledMarginDeg = Margin;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG close-hauled margin set to %.0f (boards at no-go + %.0f)"),
			Margin, Margin);
	}

	float Look = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("AILookAhead="), Look) && Look >= 0.f)
	{
		LandLookAheadSeconds = Look;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG looks %.0f s ahead for land (0 = not at all)"), Look);
	}
	float Clear = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("AILandClearance="), Clear) && Clear >= 0.f)
	{
		LandClearanceCm = Clear;
	}

	float TackAbove = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("AITackAbove="), TackAbove) && TackAbove >= 0.f)
	{
		TackAboveMS = TackAbove;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG tacks through the wind above %.1f m/s, wears below"), TackAbove);
	}
	int32 AimHigh = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("AIAimHigh="), AimHigh) && AimHigh >= 0)
	{
		// The EXISTING knob, moved out of reach in one direction or the
		// other. No new branch at the point of fire: a branch there would be
		// a second way of deciding the same thing.
		FireHighAboveRig = (AimHigh != 0) ? -1.f : 2.f;
		UE_LOG(LogTemp, Display, TEXT("AILOG fires %s, always"),
			(AimHigh != 0) ? TEXT("HIGH") : TEXT("LOW"));
	}

	int32 Refit = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("AIRefit="), Refit) && Refit >= 0)
	{
		bRefitsInPort = Refit != 0;
		UE_LOG(LogTemp, Display, TEXT("AILOG refits in port: %s"),
			bRefitsInPort ? TEXT("yes") : TEXT("no"));
	}

	int32 Prize = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("AIPrize="), Prize) && Prize >= 0)
	{
		bTakesPrizes = Prize != 0;
		UE_LOG(LogTemp, Display, TEXT("AILOG takes prizes: %s"),
			bTakesPrizes ? TEXT("yes") : TEXT("no"));
	}

	int32 Repair = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("AIRepair="), Repair) && Repair >= 0)
	{
		bRepairsAtSea = Repair != 0;
		UE_LOG(LogTemp, Display, TEXT("AILOG repairs at sea %s"),
			bRepairsAtSea ? TEXT("on") : TEXT("OFF"));
	}
	UE_LOG(LogTemp, Display, TEXT("AILOG possessed %s"),
		InPawn ? *InPawn->GetName() : TEXT("none"));
	if (AShipPawn* Ship = Cast<AShipPawn>(InPawn))
	{
		Ship->OnShipSunk.AddUObject(this, &AShipAIController::HandleOwnShipSunk);
	}
}

void AShipAIController::HandleOwnShipSunk(AShipPawn* Ship, AActor* Causer)
{
	// An explicit line, so the silence that follows is not an absence of lines.
	UE_LOG(LogTemp, Display, TEXT("AILOG abandoning ship %s"),
		Ship ? *Ship->GetName() : TEXT("?"));
	Target = nullptr;
}

AShipPawn* AShipAIController::GetShip() const
{
	return Cast<AShipPawn>(GetPawn());
}

void AShipAIController::SetPort(const FVector& Where, float RadiusCm)
{
	PortWhere = Where;
	PortRadius = RadiusCm;
	bKnowsPort = true;
}

void AShipAIController::SetDestination(const FVector& Where)
{
	Destination = Where;
	bHasDestination = true;
}

void AShipAIController::TickMerchant(AShipPawn* Me, float DeltaSeconds)
{
	// Struck or in port she lies to: sail off, helm amidships, EVERY tick.
	// SailTrimInput is a rate the pawn integrates, so a single furl order
	// holds only until something overwrites it, and a sinking clears it.
	//
	// A PRIZE IS THE EXCEPTION, and she is the reason this branch is worth
	// re-reading. A ship that has struck lies to for ever - until somebody
	// puts a crew aboard her. Then she sails again, to a destination the game
	// mode gave her, down exactly the same three lines a merchant uses to
	// make her landfall. That is the whole of "sail a prize home": no new
	// behaviour, the same seamanship pointed somewhere else.
	const bool bSailingAsPrize = Me->IsPrize() && !Me->IsSunk() && bHasDestination;
	if ((Me->IsOutOfTheFight() && !bSailingAsPrize) || !bHasDestination)
	{
		Me->SetSailTrimInput(-1.f);
		Me->SetSteerInput(0.f);
		return;
	}

	const FVector ToPort = Destination - Me->GetActorLocation();
	const UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>();
	const float WindFrom = Wind
		? FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f) : 0.f;

	// The laid course, bent off the land if there is any, then handed to the
	// rig: if it lies inside the no-go cone she beats on the nearer board,
	// held for TackHoldSeconds, exactly as a fighting ship would. What she
	// does NOT have is the wear/come-about machinery: she turns the short
	// way and takes her chances head to wind. A merchant caught in irons on
	// a course laid across the wind is a thing the log would show, and the
	// scenarios lay her course on a reach.
	float DesiredYaw = ToPort.GetSafeNormal2D().Rotation().Yaw;
	DesiredYaw = CourseClearOfLand(Me, DesiredYaw, WindFrom, DeltaSeconds, ToPort.Size2D());
	DesiredYaw = ResolveSailableHeading(DesiredYaw, WindFrom,
		Me->GetNoGoAngleDeg(), DeltaSeconds);

	const float Err = FMath::FindDeltaAngleDegrees(Me->GetActorRotation().Yaw, DesiredYaw);
	Me->SetSteerInput(FMath::Clamp(Err / FullRudderErrorDeg, -1.f, 1.f));
	// Every stitch she has. She is running for her life, and what makes her
	// slower than the ship chasing her is her hold, not her canvas.
	Me->SetSailTrimInput(1.f);

	LogTimer += DeltaSeconds;
	if (LogTimer >= 2.f)
	{
		LogTimer = 0.f;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG %s %s port=%.0fm headingErr=%.0f hull=%.0f speed=%.1fm/s windAng=%.0f rig=%.2f trim=%.2f"),
			Me->IsPrize() ? TEXT("prize") : TEXT("merchant"),
			*Me->GetName(), ToPort.Size2D() * 0.01f, Err, Me->GetHullIntegrity(),
			Me->GetForwardSpeedMS(), Me->GetWindAngleDeg(), Me->GetRigEfficiency(),
			Me->GetSailTrim());
	}
}

AShipPawn* AShipAIController::FindTarget()
{
	AShipPawn* Me = GetShip();
	if (!Me || !GetWorld())
	{
		return nullptr;
	}

	// The NEAREST ship that is on another side and still in the fight. Until
	// the convoy this returned the first hostile hull the iterator offered,
	// which was the same thing while there was only ever one; with a convoy
	// on the water and the player's idle hull a kilometre off, "first" would
	// send a raider past three merchants to go and fight the wrong ship. Every
	// existing scenario has exactly one hostile hull per side, so for them
	// nearest and first are the same hull and every number stays put.
	AShipPawn* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	const FVector Here = Me->GetActorLocation();
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		AShipPawn* Other = *It;
		if (Other->IsOutOfTheFight() || !Me->IsHostileTo(Other))
		{
			continue;
		}
		const float Dist = FVector::DistSquared2D(Here, Other->GetActorLocation());
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Other;
		}
	}
	return Best;
}

bool AShipAIController::ArcCrossesWind(float FromYaw, float ToYaw,
	float WindFromBearingDeg)
{
	// Where the eye of the wind lies relative to where we point, and how far
	// the short turn would take us. If the wind sits inside that sweep, the
	// turn goes through it.
	const float ToWindDelta = FMath::FindDeltaAngleDegrees(FromYaw, WindFromBearingDeg);
	const float TurnDelta = FMath::FindDeltaAngleDegrees(FromYaw, ToYaw);
	return (TurnDelta >= 0.f)
		? (ToWindDelta >= 0.f && ToWindDelta <= TurnDelta)
		: (ToWindDelta <= 0.f && ToWindDelta >= TurnDelta);
}

bool AShipAIController::ShotIsBlocked(const AShipPawn* Me, const FVector& FireDir,
	float TargetRange, const AShipPawn* At) const
{
	if (!Me || !GetWorld())
	{
		return false;
	}
	// Measured from the MUZZLES, not from the middle of the ship. The guns
	// stand 640 cm out on the beam (the gun port offset in AShipPawn), which at fifty metres is seven degrees of
	// bearing on its own: taking the hull centre biased the test towards
	// clearing a consort on one side and blocking one on the other.
	const FVector Muzzle = Me->GetActorLocation()
		+ FVector::CrossProduct(FVector::UpVector,
			Me->GetActorForwardVector().GetSafeNormal2D()) * 640.f
		* FMath::Sign(FVector::DotProduct(FireDir,
			FVector::CrossProduct(FVector::UpVector,
				Me->GetActorForwardVector().GetSafeNormal2D())));

	// ANY hull, on any side, blocks a lane. Round shot does not check colours,
	// and a captain who fires through a neutral to reach his enemy is doing
	// something the rule is meant to stop. Read again when allegiance landed and
	// deliberately left alone.
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		const AShipPawn* Other = *It;
		// A ship that has struck is still a ship until her deck goes under:
		// she keeps catching round shot for half a minute after she is out of
		// the fight, so she still blocks the lane.
		if (Other == Me || Other == At || !IsValid(Other))
		{
			continue;
		}
		if (Other->GetSinkPhase() >= ESinkPhase::Foundering)
		{
			continue;   // deck awash: the shot passes over her now
		}
		const FVector To = Other->GetActorLocation() - Muzzle;
		const float Range = To.Size2D();
		if (Range < 1.f || Range > TargetRange)
		{
			continue;   // astern of the mark, so not between us and it
		}
		// The angle her hull subtends at this range, plus the lane the guns'
		// own scatter needs. Thirty-one metres of ship at 300 m is six
		// degrees: wide enough that "roughly in line" really is in the way.
		const float Subtended = FMath::RadiansToDegrees(FMath::Atan2(1550.f, Range));
		const float Off = FMath::Abs(FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(To.GetSafeNormal2D(), FireDir), -1.f, 1.f))));
		if (Off <= Subtended + BlockedShotMarginDeg)
		{
			return true;
		}
	}
	return false;
}

AShipPawn* AShipAIController::FindNextAhead(const AShipPawn* Me) const
{
	// The order of battle is the game mode's spawn order. Each ship keeps
	// station on the one immediately ahead of her that is still steering, not
	// on the leader: a chain of short arms corrects itself, whereas a whole
	// line holding on one ship puts all of its accumulated error into the last
	// of them.
	const ASeaGameMode* Sea = GetWorld() ? GetWorld()->GetAuthGameMode<ASeaGameMode>() : nullptr;
	if (!Sea)
	{
		return nullptr;
	}
	AShipPawn* Ahead = nullptr;
	for (AShipPawn* Ship : Sea->GetOrderOfBattle())
	{
		if (Ship == Me)
		{
			return Ahead;
		}
		Ahead = Ship;
	}
	return nullptr;
}

float AShipAIController::ConsortAvoidance(const AShipPawn* Me) const
{
	if (!Me || !GetWorld())
	{
		return 0.f;
	}
	float Correction = 0.f;
	float Nearest = ConsortClearanceCm;
	for (TActorIterator<AShipPawn> It(GetWorld()); It; ++It)
	{
		const AShipPawn* Other = *It;
		// A consort is a hull on MY side. This read IsPlayerControlled() and
		// so counted every hull that was not the player - which, with a
		// convoy on the water, would have had a raider sheering politely away
		// from the merchant she was trying to close with. In a two-sided world
		// "not the player" and "on my side" are the same hulls, so the ten
		// existing scenarios do not move.
		if (Other == Me || !IsValid(Other)
			|| Other->GetAllegiance() != Me->GetAllegiance())
		{
			continue;
		}
		if (Other->GetSinkPhase() >= ESinkPhase::Foundering)
		{
			continue;   // she is under: nothing left to run into
		}
		const FVector To = Other->GetActorLocation() - Me->GetActorLocation();
		const float Range = To.Size2D();
		if (Range < 1.f || Range >= Nearest)
		{
			continue;
		}
		// Only what lies ahead can be run into. A consort astern is her
		// problem, not ours, and steering off for her would be a squadron
		// that scatters instead of one that keeps station.
		const float Bearing = FMath::Abs(FMath::FindDeltaAngleDegrees(
			Me->GetActorRotation().Yaw, To.Rotation().Yaw));
		if (Bearing > 75.f)
		{
			continue;
		}
		Nearest = Range;
		// Bear away from whichever side she is on. Full helm as soon as the
		// rule fires, easing off only as the distance opens: a 60-tonne hull
		// at 6.6 m/s needs six seconds of helm to move one beam, so an
		// avoidance that is weakest at the clearance boundary and strongest at
		// zero range applies almost nothing while there is still time to act
		// and everything once the ships are already touching.
		const float Side = FMath::Sign(FVector::DotProduct(To, Me->GetActorRightVector()));
		const float Closeness = FMath::Clamp(
			2.f * (1.f - Range / ConsortClearanceCm), 0.f, 1.f);
		Correction = -Side * ConsortAvoidDeg * Closeness;
	}
	return Correction;
}

float AShipAIController::ClearanceOnCourse(const AShipPawn* Me,
	float CourseYawDeg, float SpeedCmS, float ReachCapCm) const
{
	const float Far = 1.0e9f;
	if (!Me || !GetWorld() || LandLookAheadSeconds <= 0.f)
	{
		return Far;
	}

	// Her TRACK, not her heading. She makes six to nine degrees of leeway, and
	// over a ninety-second look that is seventy metres of error - wider than
	// the bank she is trying to miss - always to leeward, which is the side
	// the land is on when it matters.
	const float TrackYaw = CourseYawDeg + Me->GetLeewayDeg();
	const FVector Dir = FRotator(0.f, TrackYaw, 0.f).Vector().GetSafeNormal2D();
	const FVector From = Me->GetActorLocation();
	// Capped at the target. Land BEYOND the ship she is fighting is not in her
	// way: she will be turning long before she reaches it. Without this cap a
	// ninety-second look runs five hundred metres, straight past a target held
	// at a three-hundred-metre standoff and into the island behind it - so she
	// spent the whole action altering course for land she was never going to
	// touch. Measured: on a lee shore that left the player with 760 of her
	// hull instead of 400, which is an AI that has stopped fighting.
	const float Reach = FMath::Min(
		FMath::Max(0.f, SpeedCmS) * LandLookAheadSeconds,
		FMath::Max(1.f, ReachCapCm));

	float Worst = Far;
	for (TActorIterator<AIsland> It(GetWorld()); It; ++It)
	{
		const AIsland* Isle = *It;
		if (!IsValid(Isle))
		{
			continue;
		}
		const FVector C = Isle->GetActorLocation();
		// Closest approach of the segment [From, From + Dir*Reach] to the
		// island centre, then take off the bank's radius. Analytic, so the
		// answer does not depend on how finely anything is sampled.
		const FVector ToC = FVector(C.X - From.X, C.Y - From.Y, 0.f);
		const float Along = FMath::Clamp(FVector::DotProduct(ToC, Dir), 0.f, Reach);
		const FVector Near = From + Dir * Along;
		const float Gap = FVector::Dist2D(Near, C) - Isle->GetShoalOuterCm();
		Worst = FMath::Min(Worst, Gap);
	}
	return Worst;
}

float AShipAIController::CourseClearOfLand(const AShipPawn* Me,
	float DesiredYawDeg, float WindFromBearingDeg, float DeltaSeconds,
	float ReachCapCm)
{
	LandHoldRemaining = FMath::Max(0.f, LandHoldRemaining - DeltaSeconds);

	if (!Me || LandLookAheadSeconds <= 0.f)
	{
		return DesiredYawDeg;
	}

	const float SpeedCmS = FMath::Max(100.f, Me->GetForwardSpeedMS() * 100.f);

	// The cheap test first, and it is the whole no-island guarantee: with no
	// islands in the world ClearanceOnCourse returns a huge number, this
	// returns immediately, and not one counter moves.
	if (ClearanceOnCourse(Me, DesiredYawDeg, SpeedCmS, ReachCapCm) >= LandClearanceCm)
	{
		if (bAvoiding && LandHoldRemaining <= 0.f)
		{
			bAvoiding = false;
			UE_LOG(LogTemp, Display, TEXT("AILOG %s has the land clear again"),
				*Me->GetName());
		}
		return bAvoiding ? HeldLandYaw : DesiredYawDeg;
	}

	++AvoidTicks;

	// Hold the course already chosen until its time runs out, so she does not
	// re-decide sixty times a second and saw across her own wake.
	if (bAvoiding && LandHoldRemaining > 0.f)
	{
		return HeldLandYaw;
	}

	// Scan outwards from what she wants, taking the nearest course that clears.
	// A scan rather than a push away from the nearest island, because pushing
	// away from two islands steers exactly between them - into the gap where
	// their banks nearly touch.
	const float NoGo = Me->GetNoGoAngleDeg() + CloseHauledMarginDeg;
	float BestYaw = DesiredYawDeg;
	float BestClear = -FLT_MAX;
	bool bFound = false;

	for (float Off = LandScanStepDeg; Off <= LandScanSpanDeg + 0.1f; Off += LandScanStepDeg)
	{
		for (int32 Side = 0; Side < 2 && !bFound; ++Side)
		{
			const float Try = FMath::UnwindDegrees(
				DesiredYawDeg + (Side == 0 ? Off : -Off));
			// Never offer her a course she cannot sail: the rig would discard
			// it anyway and she would end up on a board nobody chose.
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(Try, WindFromBearingDeg)) < NoGo)
			{
				continue;
			}
			const float Clear = ClearanceOnCourse(Me, Try, SpeedCmS, ReachCapCm);
			if (Clear > BestClear)
			{
				BestClear = Clear;
				BestYaw = Try;
			}
			if (Clear >= LandClearanceCm)
			{
				bFound = true;
			}
		}
		if (bFound)
		{
			break;
		}
	}

	HeldLandYaw = BestYaw;
	LandHoldRemaining = LandHoldSeconds;
	if (!bAvoiding)
	{
		bAvoiding = true;
		++Avoidances;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG %s land ahead, altering %.0f deg to clear it (%.0f m off her track)"),
			*Me->GetName(),
			FMath::FindDeltaAngleDegrees(DesiredYawDeg, BestYaw), BestClear * 0.01f);
	}
	return BestYaw;
}

float AShipAIController::ResolveSailableHeading(float DesiredYawDeg,
	float WindFromBearingDeg, float NoGoDeg, float DeltaSeconds)
{
	TackHoldRemaining = FMath::Max(0.f, TackHoldRemaining - DeltaSeconds);

	// How close the course we want lies to the eye of the wind.
	const float ToWind = FMath::Abs(
		FMath::FindDeltaAngleDegrees(DesiredYawDeg, WindFromBearingDeg));

	// A course is only directly sailable well clear of the no-go zone, where
	// the rig actually draws. Right at the edge it gives next to nothing.
	const float Margin = NoGoDeg + CloseHauledMarginDeg;
	if (ToWind >= Margin)
	{
		TackSign = 0;
		return DesiredYawDeg;
	}

	// Upwind of where we want to be, so we beat: hold one tack for a while,
	// then the other. Flipping the moment the wind wanders never makes way.
	if (TackSign == 0 || TackHoldRemaining <= 0.f)
	{
		const float TackA = WindFromBearingDeg + Margin;
		const float TackB = WindFromBearingDeg - Margin;
		const float ErrA = FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredYawDeg, TackA));
		const float ErrB = FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredYawDeg, TackB));
		const int32 NewSign = (ErrA <= ErrB) ? 1 : -1;
		if (NewSign != TackSign)
		{
			UE_LOG(LogTemp, Display, TEXT("AILOG tack %s"),
				NewSign > 0 ? TEXT("starboard") : TEXT("port"));
		}
		TackSign = NewSign;
		TackHoldRemaining = TackHoldSeconds;
	}
	return FMath::UnwindDegrees(WindFromBearingDeg + TackSign * Margin);
}

void AShipAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AShipPawn* Me = GetShip();
	if (!Me || Me->IsSunk())
	{
		return;
	}

	// A merchant is sailed by this controller and fought by nobody. The
	// early-out is the whole of the difference: everything below is a
	// captain's, and she has no guns for it to lay.
	if (Me->GetAllegiance() == EShipAllegiance::Merchant)
	{
		TickMerchant(Me, DeltaSeconds);
		return;
	}

	RetargetTimer -= DeltaSeconds;
	// Struck or in port is as lost as sunk: a raider who went on pounding a
	// ship that had hauled down her colours would be sinking his own prize.
	if (Target && (!IsValid(Target) || Target->IsOutOfTheFight()))
	{
		UE_LOG(LogTemp, Display, TEXT("AILOG target lost %s"), *Target->GetName());
		// A ship that struck rather than sank is money lying on the water, if
		// this captain has the doctrine and the men. Guns stay silent: she is
		// going alongside, not finishing her.
		if (bTakesPrizes && IsValid(Target) && Target->HasStruck()
			&& !Target->IsSunk() && !Target->IsPrize() && !PrizeToMan)
		{
			PrizeToMan = Target;
			UE_LOG(LogTemp, Display, TEXT("AILOG %s stands by %s to put men aboard"),
				*Me->GetName(), *Target->GetName());
		}
		Target = nullptr;
	}

	// Standing by a prize OWNS the tick: no target, no guns, no tactics. She
	// is done with her when the boats have gone across (IsPrize), or when she
	// is gone.
	if (PrizeToMan)
	{
		if (!IsValid(PrizeToMan) || PrizeToMan->IsPrize() || PrizeToMan->IsSunk())
		{
			UE_LOG(LogTemp, Display, TEXT("AILOG %s has done with %s"),
				*Me->GetName(),
				IsValid(PrizeToMan) ? *PrizeToMan->GetName() : TEXT("her prize"));
			PrizeToMan = nullptr;
			bPrizeLogged = false;
			PrizeAlongsideSeconds = 0.f;
			PrizeChaseSeconds = 0.f;
		}
		else
		{
			++PrizeTicks;
			PrizeChaseSeconds += DeltaSeconds;
			const FVector ToHer = PrizeToMan->GetActorLocation() - Me->GetActorLocation();
			const float PrizeRangeM = ToHer.Size2D() * 0.01f;
			const UWindSubsystem* PrizeWind = GetWorld()->GetSubsystem<UWindSubsystem>();
			const float PrizeWindFrom = PrizeWind
				? FMath::UnwindDegrees(PrizeWind->GetWindBearingDeg() + 180.f) : 0.f;
			float Course = ToHer.GetSafeNormal2D().Rotation().Yaw;
			Course = CourseClearOfLand(Me, Course, PrizeWindFrom, DeltaSeconds,
				ToHer.Size2D());
			Course = ResolveSailableHeading(Course, PrizeWindFrom,
				Me->GetNoGoAngleDeg(), DeltaSeconds);
			const float PrizeErr = FMath::FindDeltaAngleDegrees(
				Me->GetActorRotation().Yaw, Course);
			Me->SetSteerInput(FMath::Clamp(PrizeErr / FullRudderErrorDeg, -1.f, 1.f));
			// Take in sail as she comes up, so she lies to instead of sailing
			// past; set it again if she has fallen away.
			Me->SetSailTrimInput(PrizeRangeM <= PrizeLieToM ? -1.f : 1.f);
			if (PrizeRangeM <= PrizeLieToM)
			{
				PrizeAlongsideSeconds += DeltaSeconds;
				if (!bPrizeLogged)
				{
					bPrizeLogged = true;
					UE_LOG(LogTemp, Display, TEXT("AILOG %s lies to by %s at %.0f m"),
						*Me->GetName(), *PrizeToMan->GetName(), PrizeRangeM);
				}
			}
			// The boats should have gone across long ago. They have not, which
			// means she cannot spare the men: make sail and get on with the
			// cruise. The prize stays struck and stays counted; she is simply
			// nobody's.
			if (PrizeAlongsideSeconds >= PrizeGiveUpSeconds
				|| PrizeChaseSeconds >= PrizeAbandonSeconds)
			{
				UE_LOG(LogTemp, Display,
					TEXT("AILOG %s gives up %s after %.0f s alongside, %.0f s in all"),
					*Me->GetName(), *PrizeToMan->GetName(), PrizeAlongsideSeconds,
					PrizeChaseSeconds);
				PrizeToMan = nullptr;
				bPrizeLogged = false;
				PrizeAlongsideSeconds = 0.f;
				PrizeChaseSeconds = 0.f;
			}
			else
			{
				return;
			}
		}
	}
	// A target is kept until she is out of the fight, and the search runs
	// only while there is none. This used to re-pick every two seconds, which
	// was harmless with one hostile hull in the world and ruinous with two
	// merchants abeam of each other 150 m apart: the raider put four balls
	// into one's rigging, "nearest" flipped to the other as they crossed, she
	// put three into that one, and neither was hurt enough to strike. A
	// captain who has cut up a ship's rig finishes her. With one hostile hull
	// the search returns the same ship it always did, so nothing already
	// measured moves.
	if (!Target && RetargetTimer <= 0.f)
	{
		Target = FindTarget();
		RetargetTimer = 2.f;
	}

	if (!Target)
	{
		// Aground with nobody to fight is not a reason to do nothing - it is
		// the worst case there is. Furling to zero here would take her rudder
		// with it (backed-sail authority dies below a trim of 0.1) and leave
		// her on the bank for ever, and this branch is reached in ordinary
		// play every time the player sinks, for the thirty-five seconds before
		// a new ship is given to them.
		const FVector Escape = Me->GetGroundOffshoreDir();
		if (Me->IsAground() && !Escape.IsNearlyZero())
		{
			++LandTicks;
			if (!bClawingOff)
			{
				bClawingOff = true;
				++ClawOffs;
				UE_LOG(LogTemp, Display,
					TEXT("AILOG %s aground with no target, clawing off: offshore bearing %.0f"),
					*Me->GetName(), Escape.Rotation().Yaw);
			}

			UWindSubsystem* NoTargetWind = GetWorld()->GetSubsystem<UWindSubsystem>();
			const float WindFrom = NoTargetWind
				? FMath::UnwindDegrees(NoTargetWind->GetWindBearingDeg() + 180.f) : 0.f;
			// Same as the main branch: a held board would otherwise be
			// returned in place of the offshore course, unread.
			const float NoTargetMargin = Me->GetNoGoAngleDeg() + CloseHauledMarginDeg;
			const float NoTargetBoard = FMath::UnwindDegrees(
				WindFrom + TackSign * NoTargetMargin);
			if (TackSign != 0 && FMath::Cos(FMath::DegreesToRadians(
				FMath::FindDeltaAngleDegrees(NoTargetBoard, Escape.Rotation().Yaw))) <= 0.f)
			{
				TackHoldRemaining = 0.f;
			}
			const float Course = ResolveSailableHeading(Escape.Rotation().Yaw,
				WindFrom, Me->GetNoGoAngleDeg(), DeltaSeconds);
			const float Err = FMath::FindDeltaAngleDegrees(
				Me->GetActorRotation().Yaw, Course);
			Me->SetSteerInput(FMath::Clamp(Err / FullRudderErrorDeg, -1.f, 1.f));
			Me->SetSailTrimInput(
				(Me->GetGroundClosingMS() > StillDrivingOnMS
					&& Me->GetSailTrim() > ClawOffTrimFloor) ? -1.f : 1.f);
			bWearing = false;
			return;
		}
		if (bClawingOff)
		{
			bClawingOff = false;
			UE_LOG(LogTemp, Display, TEXT("AILOG %s clear of the ground"), *Me->GetName());
		}

		// Nothing to fight: take in sail rather than ghost on across the sea
		// under full canvas with nobody at the helm.
		Me->SetSailTrimInput(-1.f);
		Me->SetSteerInput(0.f);
		bWearing = false;
		return;
	}

	// MAKING FOR PORT. After the prize block on purpose: a ship standing by a
	// prize has already paid the men, and leaving before the boats go across
	// would waste them. Before the fight, because a refit is a decision to
	// stop fighting.
	if (bRefitsInPort && bKnowsPort)
	{
		const float HullFrac = Me->GetHullIntegrity()
			/ FMath::Max(1.f, Me->GetMaxHullIntegrity());
		// What is in the coffers decides whether the trip is worth making.
		const ASeaGameMode* Sea = GetWorld()
			? GetWorld()->GetAuthGameMode<ASeaGameMode>() : nullptr;
		const int32 Coffers = Sea ? Sea->GetCoffers() : 0;
		const bool bDry = Me->HasMagazine()
			&& Me->GetShot() < RefitBelowShot * Me->GetShotMax();
		const bool bWants = (Me->GetHandsShort() >= RefitWhenShort
			|| HullFrac < RefitBelowHull || bDry) && Coffers >= RefitNeedsCoffers;
		const float ToPortM = FVector::Dist2D(Me->GetActorLocation(), PortWhere) * 0.01f;
		if (bWants)
		{
			++PortTicks;
			if (!bPortLogged)
			{
				bPortLogged = true;
				UE_LOG(LogTemp, Display,
					TEXT("AILOG %s bears away for the port: %d hands short, hull %.0f%%, %.0f m, coffers %d"),
					*Me->GetName(), Me->GetHandsShort(), HullFrac * 100.f, ToPortM,
					Coffers);
			}
			const UWindSubsystem* PortWind = GetWorld()->GetSubsystem<UWindSubsystem>();
			const float PortWindFrom = PortWind
				? FMath::UnwindDegrees(PortWind->GetWindBearingDeg() + 180.f) : 0.f;
			const FVector ToIt = PortWhere - Me->GetActorLocation();
			float PortCourse = ToIt.GetSafeNormal2D().Rotation().Yaw;
			PortCourse = CourseClearOfLand(Me, PortCourse, PortWindFrom, DeltaSeconds,
				ToIt.Size2D());
			PortCourse = ResolveSailableHeading(PortCourse, PortWindFrom,
				Me->GetNoGoAngleDeg(), DeltaSeconds);
			const float PortErr = FMath::FindDeltaAngleDegrees(
				Me->GetActorRotation().Yaw, PortCourse);
			Me->SetSteerInput(FMath::Clamp(PortErr / FullRudderErrorDeg, -1.f, 1.f));
			// Inside the roadstead she lies to and lets the port work on her.
			Me->SetSailTrimInput(ToIt.Size2D() <= PortRadius ? -1.f : 1.f);
			return;
		}
		if (bPortLogged)
		{
			bPortLogged = false;
			UE_LOG(LogTemp, Display,
				TEXT("AILOG %s is refitted and stands out again t=%.1f"),
				*Me->GetName(), GetWorld()->GetTimeSeconds());
		}
	}

	const FVector MyLoc = Me->GetActorLocation();
	const FVector ToTarget = Target->GetActorLocation() - MyLoc;
	const float RangeM = ToTarget.Size2D() * 0.01f;
	const FVector DirToTarget = ToTarget.GetSafeNormal2D();
	const float BearingToTarget = DirToTarget.Rotation().Yaw;

	// --- pick a tactic --------------------------------------------------
	const float HullFraction = Me->GetHullIntegrity()
		/ FMath::Max(1.f, Me->GetMaxHullIntegrity());
	if (HullFraction <= DisengageHullFraction)
	{
		Tactic = EShipTactic::Disengage;
	}
	else if (RangeM > EngageRangeM)
	{
		Tactic = EShipTactic::Close;
	}
	else
	{
		Tactic = EShipTactic::Engage;
	}

	// --- the hands ---------------------------------------------------------
	// Hurt and with nothing to shoot at, she repairs; in range, every man to
	// the guns. SetRepairShare is idempotent and logs only a change.
	{
		const bool bHurt = Me->GetRigEfficiency() < RepairBelow
			|| Me->GetRudderIntegrity() < RepairBelow;
		const bool bGunsIdle = RangeM > EngageRangeM || Tactic == EShipTactic::Disengage;
		Me->SetRepairShare((bRepairsAtSea && bHurt && bGunsIdle) ? RepairShareWhenHurt : 0.f);
	}

	// --- work out the course we want ------------------------------------
	UWindSubsystem* Wind = GetWorld()->GetSubsystem<UWindSubsystem>();
	const float WindFromBearing = Wind
		? FMath::UnwindDegrees(Wind->GetWindBearingDeg() + 180.f) : 0.f;

	float DesiredYaw = BearingToTarget;

	switch (Tactic)
	{
	case EShipTactic::Close:
		// Straight at her.
		break;

	case EShipTactic::Engage:
	{
		// Turn across her so the guns bear, easing in or out to hold the
		// standoff. Either side works geometrically, so take the one that is
		// further from the eye of the wind: circling on the windward side put
		// the ship head to wind and stopped her dead every time.
		const float RangeError = RangeM - StandoffM;
		// Three regimes, see FiringBandM: inside the standoff she eases out as
		// she always did; in the band she is beam-on and the guns bear; beyond
		// it she leads in steeply enough to actually close.
		const float Lead = RangeError > 0.f
			? FMath::Clamp((RangeError - FiringBandM) * OutsideLeadDegPerM, 0.f, 55.f)
			: FMath::Clamp(RangeError * 0.25f, -55.f, 0.f);

		const float TurnRight = FMath::UnwindDegrees(BearingToTarget + 90.f - Lead);
		const float TurnLeft = FMath::UnwindDegrees(BearingToTarget - 90.f + Lead);

		// Either side lays the guns. Take the SHORTER turn, unless that heading
		// lies inside the no-go zone, in which case take the other. Choosing
		// purely by distance from the wind once sent her 147 degrees round
		// through the eye of the wind with no way on, at a degree a second.
		const float MyYaw = Me->GetActorRotation().Yaw;
		const float NoGo = Me->GetNoGoAngleDeg() + CloseHauledMarginDeg;
		auto Sailable = [&](float Yaw)
		{
			return FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, WindFromBearing)) >= NoGo;
		};
		// A turn that stays clear of the wind is worth more than a short one:
		// crossing the eye of the wind costs a minute of standing still.
		auto TurnCost = [&](float Yaw)
		{
			const float Sweep = FMath::Abs(FMath::FindDeltaAngleDegrees(MyYaw, Yaw));
			return ArcCrossesWind(MyYaw, Yaw, WindFromBearing) ? Sweep + 360.f : Sweep;
		};
		const float TurnRightCost = TurnCost(TurnRight);
		const float TurnLeftCost = TurnCost(TurnLeft);

		const float Nearer = (TurnRightCost <= TurnLeftCost) ? TurnRight : TurnLeft;
		const float Farther = (TurnRightCost <= TurnLeftCost) ? TurnLeft : TurnRight;
		DesiredYaw = Sailable(Nearer) ? Nearer : (Sailable(Farther) ? Farther : Nearer);

		// Being outrun. Outside her standoff and not closing, she runs straight
		// down on her chase and lays the guns when she gets there; see
		// PursuitBelowMS for what this looked like before. Against a target
		// that is not making off the closing speed is her own approach speed
		// and this never fires - which is what keeps every fight already
		// measured exactly where it was.
		const FVector RelVel = Target->GetVelocity() - Me->GetVelocity();
		const float ClosingMS = -FVector::DotProduct(RelVel, DirToTarget) * 0.01f;
		// Once running down she keeps at it until she is inside the slack;
		// once she has laid the guns she keeps them laid until the range has
		// opened past the resume distance AND the laid course is not closing.
		const bool bRunningDown = bPursuing
			? RangeM > StandoffM + PursuitSlackM
			: (RangeM > StandoffM + PursuitResumeM && ClosingMS < PursuitBelowMS);
		if (bRunningDown)
		{
			DesiredYaw = BearingToTarget;
			++PursuitTicks;
			if (!bPursuing)
			{
				bPursuing = true;
				UE_LOG(LogTemp, Display,
					TEXT("AILOG %s running down %s: range %.0f m, closing %.1f m/s"),
					*Me->GetName(), *Target->GetName(), RangeM, ClosingMS);
			}
		}
		if (!bRunningDown && bPursuing)
		{
			bPursuing = false;
			UE_LOG(LogTemp, Display, TEXT("AILOG %s lays the guns again at %.0f m"),
				*Me->GetName(), RangeM);
		}
		break;
	}

	case EShipTactic::Disengage:
		// Put the wind dead astern, which is the fastest way out for a square rig.
		DesiredYaw = FMath::UnwindDegrees(WindFromBearing + 180.f);
		break;
	}

	// A follower keeps station on her next ahead instead of choosing her own
	// course. Only the leader navigates, so the measured Close/Engage/Disengage
	// behaviour is preserved exactly as it was tuned, and the line does the
	// one thing a line of battle is for: it puts every consort ahead of or
	// astern of the guns, ninety degrees from where they can ever train.
	AShipPawn* NextAhead = FindNextAhead(Me);
	bool bKeepingStation = false;
	if (IsValid(NextAhead) && !NextAhead->IsSunk() && Tactic != EShipTactic::Disengage)
	{
		const FVector AheadDir = NextAhead->GetActorForwardVector().GetSafeNormal2D();
		const FVector Station = NextAhead->GetActorLocation() - AheadDir * LineIntervalCm;
		const FVector Offset = Me->GetActorLocation() - Station;
		const float CrossErrM = FVector::CrossProduct(AheadDir, Offset).Z * 0.01f;
		const float AlongErrM = FVector::DotProduct(AheadDir, Offset) * 0.01f;

		// Steer her next ahead's COURSE, bent by how far off the wake we lie.
		// Steering at the station point itself would make the whole tail cut
		// every corner, because a point astern of a turning ship lies inside
		// her turn.
		//
		// That holds while she is NEAR her station. Far off it the same rule
		// is useless: a course parallel to a line she is kilometres from never
		// closes the gap, because the correction is a bounded angle and the
		// error is a distance. Past RejoinAboveCm she steers at the station
		// POINT instead, which is rejoining rather than station keeping - the
		// distinction this file's own comment drew and never acted on.
		if (Offset.Size2D() > RejoinAboveCm)
		{
			DesiredYaw = (Station - Me->GetActorLocation()).Rotation().Yaw;
			++RejoinTicks;
			if (!bRejoining)
			{
				bRejoining = true;
				UE_LOG(LogTemp, Display,
					TEXT("AILOG %s is %.0f m off station, rejoining"),
					*Me->GetName(), Offset.Size2D() * 0.01f);
			}
		}
		else
		{
			if (bRejoining)
			{
				bRejoining = false;
				UE_LOG(LogTemp, Display, TEXT("AILOG %s back in the line"), *Me->GetName());
			}
			const float Correction = FMath::Clamp(-CrossErrM * StationCrossGainDegPerM,
				-StationMaxCorrectionDeg, StationMaxCorrectionDeg);
			DesiredYaw = FMath::UnwindDegrees(NextAhead->GetActorRotation().Yaw + Correction);
		}

		// Interval is held with canvas, because canvas is the only throttle a
		// square rig has. Positive along-error means she is overhauling - but
		// that throttle belongs to a ship who is ON her station. A ship three
		// intervals away is rejoining, and the along-error then says nothing
		// useful: measured, a follower thrown off by a bank was ordered to
		// SHORTEN sail because the station point had drawn ahead of her on the
		// other axis, while she was trying to catch a line doing six knots.
		Me->SetSailTrimInput(bRejoining ? 1.f
			: (AlongErrM > StationSlackM ? -1.f
				: (AlongErrM < -StationSlackM ? 1.f : 0.f)));
		bKeepingStation = true;

		if (!bReportedStation)
		{
			bReportedStation = true;
			UE_LOG(LogTemp, Display, TEXT("AILOG %s takes station astern of %s"),
				*Me->GetName(), *NextAhead->GetName());
		}
	}
	else if (!bReportedStation)
	{
		bReportedStation = true;
		UE_LOG(LogTemp, Display, TEXT("AILOG %s has the lead"), *Me->GetName());
	}

	// --- the ground ------------------------------------------------------
	//
	// A REPLACEMENT of the course, not a correction to it, and placed BEFORE
	// the rig has its say. Both of those are forced by the ordering.
	//
	// A correction added before ResolveSailableHeading is silently discarded
	// on exactly the close-hauled legs where land matters, because that
	// function throws the desired course away whole when it lies inside the
	// cone and returns the held tack instead - the ConsortAvoidance bug,
	// already paid for once. A correction added AFTER it produces a heading
	// inside the cone where the drive is exactly zero, which stops her, which
	// makes ArcCrossesWind true, which sets bWearing, which brings her round
	// through downwind and onto the beach. The avoidance would be the
	// grounding.
	//
	// A replacement has neither failure: there is nothing left to discard, and
	// whatever comes back out is sailable because ResolveSailableHeading
	// guarantees it. Straight offshore on a lee shore lies inside the cone by
	// definition, and what comes back is then the nearer close-hauled board,
	// held for TackHoldSeconds. That is a ship beating off a lee shore, built
	// entirely out of machinery that was already calibrated.
	const FVector Offshore = Me->GetGroundOffshoreDir();
	const bool bOnTheGround = Me->IsAground() && !Offshore.IsNearlyZero();
	if (bOnTheGround)
	{
		++LandTicks;
		DesiredYaw = Offshore.Rotation().Yaw;

		// A wear in progress owns the helm outright - SetSteerInput is given
		// WearSign, not the heading error - and a wear comes round through
		// DOWNWIND, which on a lee shore is up the beach. Whatever she was in
		// the middle of, she is not in the middle of it any more.
		bWearing = false;

		// And the held tack must not swallow the offshore course. This is the
		// half of the ordering argument that was wrong: a REPLACEMENT is
		// discarded by ResolveSailableHeading just as completely as a
		// correction was, only in a different branch - when a board is already
		// held, the function returns wind +/- margin and never reads the
		// course it was given at all, for as long as 45 seconds. So zero the
		// hold when she first touches, and again whenever the board she is
		// holding has no offshore component left in it. The re-pick then
		// chooses from the offshore bearing and reloads the hold itself, so
		// this cannot flap: the nearer of two boards 140 degrees apart is
		// always within 70 degrees of what was asked for.
		const float BoardMargin = Me->GetNoGoAngleDeg() + CloseHauledMarginDeg;
		const float HeldBoard = FMath::UnwindDegrees(
			WindFromBearing + TackSign * BoardMargin);
		const bool bHeldBoardGoesAshore = TackSign != 0
			&& FMath::Cos(FMath::DegreesToRadians(
				FMath::FindDeltaAngleDegrees(HeldBoard, DesiredYaw))) <= 0.f;
		if (!bClawingOff || bHeldBoardGoesAshore)
		{
			TackHoldRemaining = 0.f;
		}

		if (!bClawingOff)
		{
			bClawingOff = true;
			++ClawOffs;
			UE_LOG(LogTemp, Display,
				TEXT("AILOG %s aground, clawing off: offshore bearing %.0f, wind from %.0f"),
				*Me->GetName(), DesiredYaw, WindFromBearing);
		}
	}
	else if (bClawingOff)
	{
		bClawingOff = false;
		bClawLogged = false;
		UE_LOG(LogTemp, Display, TEXT("AILOG %s clear of the ground"), *Me->GetName());
	}

	// Land ahead. Placed before the rig has its say, for the same reason the
	// claw-off is: a course handed to ResolveSailableHeading is either kept or
	// replaced by a board, and both of those are sailable. It is skipped while
	// she is aground, because then the claw-off already owns the course and
	// looking ninety seconds ahead of a stopped ship answers nothing.
	if (!bOnTheGround)
	{
		// She looks as far as the target and no further, plus one standoff so
		// she still sees what lies just past the ship she is circling.
		DesiredYaw = CourseClearOfLand(Me, DesiredYaw, WindFromBearing, DeltaSeconds,
			ToTarget.Size2D() + StandoffM * 100.f);
	}

	const float AskedYaw = DesiredYaw;
	DesiredYaw = ResolveSailableHeading(DesiredYaw, WindFromBearing,
		Me->GetNoGoAngleDeg(), DeltaSeconds);

	if (bOnTheGround && !bClawLogged)
	{
		// What she ASKED for and what the rig gave her, side by side. Without
		// this pair, "she came off in 42 seconds" cannot be told apart from
		// "the tack hold expired after 45 and she happened to sail away" -
		// both fit the same log, and the second one is a slice that does
		// nothing while every counter agrees with me.
		bClawLogged = true;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG %s clawing: asked %.0f, steering %.0f, tack %d, hold %.0f"),
			*Me->GetName(), AskedYaw, DesiredYaw, TackSign, TackHoldRemaining);
	}

	// Sheer off a consort close aboard, applied AFTER the rig has had its say.
	// Applied before, it was silently discarded on every close-hauled leg:
	// ResolveSailableHeading throws the desired course away entirely when it
	// lies inside the no-go cone and returns the held tack instead, which is
	// exactly the point of sail on which ships bunch up.
	// Not while she is on the ground. The sheer is up to 55 degrees and is
	// applied AFTER the rig has had its say, so on a claw-off it can carry the
	// head straight back inside the no-go cone, where the drive is zero. A
	// ship aground is making no way and cannot run anyone down anyway; the
	// consorts still afloat go on avoiding HER, because each ship computes her
	// own sheer.
	const float Sheer = bOnTheGround ? 0.f : ConsortAvoidance(Me);
	if (!FMath::IsNearlyZero(Sheer))
	{
		DesiredYaw = FMath::UnwindDegrees(DesiredYaw + Sheer);
		UE_LOG(LogTemp, Verbose, TEXT("AILOG %s sheering %.0f"), *Me->GetName(), Sheer);
	}

	// --- rudder and sail -------------------------------------------------
	const float MyYawNow = Me->GetActorRotation().Yaw;
	const float HeadingError = FMath::FindDeltaAngleDegrees(MyYawNow, DesiredYaw);

	// Wearing ship. A square rig only crosses the eye of the wind with real
	// way on; caught halfway she stops and stays there. Measured before this
	// existed: two stalls of about eighty seconds in one engagement, five
	// broadsides in the first ninety seconds and none in the next two
	// hundred. So when the short way round passes through the wind, put the
	// helm over the other way and come round through downwind instead.
	if (bComingAbout && (FMath::Abs(HeadingError) <= WearDoneErrorDeg
		|| !ArcCrossesWind(MyYawNow, DesiredYaw, WindFromBearing)))
	{
		bComingAbout = false;
		UE_LOG(LogTemp, Display, TEXT("AILOG %s came about, speed %.2f"),
			*Me->GetName(), Me->GetForwardSpeedMS());
	}

	if (bWearing)
	{
		if (FMath::Abs(HeadingError) <= WearDoneErrorDeg ||
			!ArcCrossesWind(MyYawNow, DesiredYaw, WindFromBearing))
		{
			bWearing = false;
			UE_LOG(LogTemp, Display, TEXT("AILOG wore round, heading error %.0f"),
				HeadingError);
		}
	}
	else if (ArcCrossesWind(MyYawNow, DesiredYaw, WindFromBearing)
		&& Me->GetForwardSpeedMS() >= TackAboveMS && !bComingAbout)
	{
		bComingAbout = true;
		++TacksThrough;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG %s coming about, error %.0f, speed %.2f"),
			*Me->GetName(), HeadingError, Me->GetForwardSpeedMS());
	}
	else if (!bComingAbout
		&& ArcCrossesWind(MyYawNow, DesiredYaw, WindFromBearing)
		&& Me->GetForwardSpeedMS() < TackAboveMS)
	{
		// Not while she is already going about. A ship crossing the wind is
		// SLOWING as she crosses it, so without this guard the come-about
		// turns itself into a wear halfway through, every time: she loses the
		// speed that authorised the tack and the next tick sends her round the
		// other way instead. Measured with the guard missing: four
		// come-abouts and five wears in the same run, and not a metre gained.
		// Wearing is what you do when you have not got the way on to tack.
		// With way on, put the helm down and go about: it costs seconds, not
		// the two hundred metres to leeward a wear costs.
		bWearing = true;
		WearSign = (HeadingError >= 0.f) ? -1.f : 1.f;
		++Wears;
		UE_LOG(LogTemp, Display,
			TEXT("AILOG %s wearing ship to %s, error %.0f, speed %.2f"),
			*Me->GetName(), WearSign > 0.f ? TEXT("starboard") : TEXT("port"),
			HeadingError, Me->GetForwardSpeedMS());
	}

	Me->SetSteerInput(bWearing
		? WearSign
		: FMath::Clamp(HeadingError / FullRudderErrorDeg, -1.f, 1.f));

	if (bOnTheGround)
	{
		// Canvas is what holds her on: the same ship furled came off in 94
		// seconds and under full sail lay there for 190. So while she is still
		// standing further in, take it in; once she has stopped driving on,
		// set it again and sail off the course the rig just gave her.
		//
		// Never all the way off. Below a trim of 0.1 the backed-sail authority
		// goes, and with it the only steering a square rig has at rest - which
		// would leave her pointing wherever the swell put her.
		Me->SetSailTrimInput(
			(Me->GetGroundClosingMS() > StillDrivingOnMS && Me->GetSailTrim() > ClawOffTrimFloor)
				? -1.f : 1.f);
	}
	else if (!bKeepingStation)
	{
		Me->SetSailTrimInput(1.f);
	}

	// --- gunnery ---------------------------------------------------------
	if (Tactic != EShipTactic::Disengage && RangeM <= EngageRangeM)
	{
		const float OffBeamStarboard = FMath::Abs(FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(
				Me->GetActorRightVector(), DirToTarget), -1.f, 1.f))));
		const float OffBeamPort = 180.f - OffBeamStarboard;

		// Cripple her, then sink her.
		const bool bHigh = Target->GetRigEfficiency() > FireHighAboveRig;
		const float TargetRange = ToTarget.Size2D();

		const bool bStarboardBears =
			OffBeamStarboard <= FiringArcDeg && Me->GetReloadRemaining(true) <= 0.f;
		const bool bPortBears =
			OffBeamPort <= FiringArcDeg && Me->GetReloadRemaining(false) <= 0.f;

		if (bStarboardBears || bPortBears)
		{
			// A consort in the lane means the guns stay silent. Measured
			// before this existed: one enemy put a ball into another while
			// the player never fired a shot.
			if (ShotIsBlocked(Me, DirToTarget, TargetRange, Target))
			{
				if (!bHeldFire)
				{
					bHeldFire = true;
					UE_LOG(LogTemp, Display,
						TEXT("AILOG %s holds fire, consort in the lane"), *Me->GetName());
				}
			}
			else
			{
				bHeldFire = false;
				Me->FireBroadside(bStarboardBears, Target, bHigh);
			}
		}
	}

	LogTimer += DeltaSeconds;
	if (LogTimer >= 2.f)
	{
		LogTimer = 0.f;
		const TCHAR* TacticName =
			(Tactic == EShipTactic::Close) ? TEXT("close")
			: (Tactic == EShipTactic::Engage) ? TEXT("engage") : TEXT("disengage");
		UE_LOG(LogTemp, Display,
			TEXT("AILOG tactic=%s range=%.0fm headingErr=%.0f hull=%.0f speed=%.1fm/s windAng=%.0f wearing=%d rig=%.2f tgtRig=%.2f target=%s"),
			TacticName, RangeM, HeadingError, Me->GetHullIntegrity(),
			Me->GetForwardSpeedMS(), Me->GetWindAngleDeg(), bWearing ? 1 : 0,
			Me->GetRigEfficiency(), Target->GetRigEfficiency(),
			*Target->GetName());

	}
}
