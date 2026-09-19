#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ShipPawn.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UBuoyancyComponent;
class USpringArmComponent;
class UCameraComponent;
class UWaterBodyComponent;
class UWindSubsystem;
class ACannonBall;

/** Who a hull belongs to.
 *
 *  Until now "enemy" meant IsPlayerControlled(), which is true of exactly one
 *  hull and says nothing about sides. That was enough for a two-sided world and
 *  is not enough for a third party who is nobody's enemy until somebody fires:
 *  a merchant is not the player's friend, and she is not the Crown's quarry.
 *
 *  In today's two-sided world "allegiance differs" is arithmetically identical
 *  to "is player controlled", which is the point of landing it on its own: the
 *  whole measurement suite has to come back unmoved, and if it does not, the
 *  model is wrong and nothing has been built on top of it yet. */
UENUM()
enum class EShipAllegiance : uint8
{
	/** The hull the person is steering. */
	Player,
	/** The squadron sent to sink her. */
	Crown,
	/** Cargo. Fires at nobody; anybody may fire at her. */
	Merchant
};

/** Where a hull is on its way down. Afloat is the only state a ship fights in. */
UENUM()
enum class ESinkPhase : uint8
{
	Afloat,
	/** Taking water: lift comes off the holed side, she settles and lists. */
	Flooding,
	/** Deck awash. She hangs there a moment before she goes. */
	Foundering,
	/** The last of the lift runs out and she slides under. */
	Plunging,
	/** Deep enough to be nobody's problem. Destroyed right after. */
	Wreck
};

/** Where a shot struck. What it costs her depends entirely on this. */
UENUM()
enum class EShipZone : uint8
{
	/** Below the rail. Holes her, and enough of them sink her. */
	Hull,
	/** Fore mast, its yards and its courses. Costs her sail. */
	ForeRig,
	/** Main mast, taller and carrying more canvas. Costs her more sail. */
	MainRig,
	/** Right aft and low, reachable only through the transom. Costs her steering. */
	Rudder,
	/** A gun port. Dismounts that gun, so the side fires one ball fewer. */
	Guns
};

/** Fired once, the instant integrity reaches zero: she is out of the fight. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShipSunk, AShipPawn* /*Ship*/, AActor* /*Causer*/);
/** Fired once, just before the wreck is destroyed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnShipWrecked, AShipPawn* /*Ship*/);
/** Fired once, when she hauls down her colours. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnShipStruck, AShipPawn* /*Ship*/, AActor* /*Causer*/);

/**
 * A sailing ship driven by real physics and by the wind.
 *
 * The root is a box primitive because UBuoyancyComponent applies its forces to
 * whatever primitive is the actor's root - it does a Cast<UPrimitiveComponent>
 * on GetRootComponent(). The visible hull hangs off that root with no collision
 * of its own, so the rigging never participates in physics.
 *
 * Speed is not a throttle. It comes out of three things: how much sail is set,
 * how hard the wind blows, and the angle between the wind and the hull. A
 * square rig cannot sail into the wind at all, and does its best work with the
 * wind on the quarter.
 */
UCLASS()
class PIRATESEAS_API AShipPawn : public APawn
{
	GENERATED_BODY()

public:
	AShipPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Metres per second along the hull's forward axis, signed. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetForwardSpeedMS() const;

	/** 0 = furled, 1 = every sail set. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetSailTrim() const { return SailTrim; }

	/** Degrees between the hull's heading and where the wind comes FROM.
	 *  0 means the wind is dead ahead and the ship is in irons. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetWindAngleDeg() const { return WindAngleDeg; }

	/** Signed angle between where the bow points and where the ship actually
	 *  goes, in degrees. Positive means the track lies to starboard of the
	 *  heading, so she is being set to starboard. This is leeway, and it is
	 *  the difference between a course steered and a course made good. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetLeewayDeg() const { return LeewayDeg; }

	/** Ground gained straight into the wind, in metres per second. Negative
	 *  means she is losing ground to leeward however fast she is sailing. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetWindwardVMG() const { return WindwardVMG; }

	/** How much drive the rig gets at a given wind angle, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float SailDriveCoefficient(float AbsWindAngleDeg) const;

	/** Fires every loaded gun on one side. Does nothing while reloading.
	 *  With a target, the crews set elevation for its range and traverse the
	 *  guns towards it within MaxTraverseDeg; without one they fire along the
	 *  beam at the default elevation. */
	/** With bHigh the crews point at the target's rig instead of her hull:
	 *  it will never sink her, but it takes the way off her. */
	UFUNCTION(BlueprintCallable, Category = "Guns")
	bool FireBroadside(bool bStarboard, AActor* AimAt = nullptr, bool bHigh = false);

	// ---- laying the guns by hand -----------------------------------------
	/** True once the player has laid the guns himself. Until then nothing about
	 *  this feature is reachable, which is deliberate: the AI captain and the
	 *  -ShipFireTest harness go down the old path untouched, so not one of the
	 *  twenty-six measured scenarios moves because of it. An aiming system that
	 *  re-baselined the suite on the day it was added would have hidden its own
	 *  effect inside a hundred other changed numbers. */
	bool IsLayingByHand() const { return bLayingByHand; }

	/** Where the guns are pointed, in degrees off the beam, positive forward.
	 *  Always inside +/-MaxTraverseDeg: the carriage stop is not advisory, and
	 *  seeing the guns REFUSE to follow the mouse any further is the whole way
	 *  the player learns that the ship has to be turned. */
	float GetLayTrainDeg() const { return LayTrainDeg; }

	/** Barrel elevation the player has wound on, degrees. */
	float GetLayElevationDeg() const { return LayElevationDeg; }

	/** Which side the guns are laid on: the side the player is looking at. */
	bool IsLayingStarboard() const { return bLayStarboard; }

	/** The lay as a ROTATION ABOUT UP, which is not the same number as the lay
	 *  itself and is the reason the mouse ran backwards to starboard for a day.
	 *
	 *  GetLayTrainDeg() is stored positive-FORWARD on both sides, because that is
	 *  what a HUD and a log line want: one number that reads the same whichever
	 *  battery is up. But a positive rotation about the world's up axis carries
	 *  the STARBOARD beam AFT and the PORT beam FORWARD - opposite senses - so
	 *  feeding the stored number straight to RotateAngleAxis mirrors one side.
	 *
	 *  Everything that turns a beam vector calls THIS, and nothing works the sign
	 *  out for itself. Three places each deriving it separately is how the bug
	 *  was born. */
	float LayRotationDeg() const { return LayTrainDeg * (bLayStarboard ? -1.f : 1.f); }

	/** How far forward of the beam the guns actually point, as a fraction: +1 is
	 *  dead ahead, 0 is square abeam, -1 is dead astern. Derived from the WORLD
	 *  direction rather than from the stored angle, so it is the one number that
	 *  can catch the sign being wrong - the stored angle reads +6 on both sides
	 *  whether or not the guns agree with it. */
	float LayForwardDot() const;

	/** True while the guns are pegged dead abeam and ignoring the mouse. */
	bool IsLayLocked() const { return bLayLocked; }

	/** True when the mouse is asking for more train than the carriages have,
	 *  i.e. the guns are hard against the stop. This is the signal the picture
	 *  is drawn from, and it is the one the player has to feel. */
	bool IsAgainstTheStop() const { return bAgainstStop; }

	/** Ground range the current elevation drops a ball at, centimetres. The
	 *  inverse of ElevationForRangeDeg, and the number the fall-of-shot mark is
	 *  drawn at - without it the player would be winding a knob with no reading
	 *  on it, which is not aiming, it is guessing. */
	float RangeForElevationCm(float ElevationDeg) const;

	/** How high the gun ports sit above the hull origin, centimetres. */
	float GetGunPortHeightCm() const;

	/** The water body this ship floats on, so the aim marks can be laid on the
	 *  same Gerstner surface she rides rather than on a flat plane at zero. The
	 *  splash code learned that lesson the hard way: tested against the plane,
	 *  twenty-five splashes in the logs and not one above Z=0. */
	UWaterBodyComponent* GetWaterBody() const { return PrimaryWaterBody; }

	/** Barrel elevation that drops a shot at the given ground range, from the
	 *  flat-water ballistic arc, corrected by the measured drag shortfall. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	float ElevationForRangeDeg(float RangeCm) const;

	/** Nearest other ship that lies on the given side, or null. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	AShipPawn* FindTargetOnSide(bool bStarboard) const;

	/** Seconds left before that side can fire again. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	float GetReloadRemaining(bool bStarboard) const;

	UFUNCTION(BlueprintPure, Category = "Ship")
	float GetHullIntegrity() const { return HullIntegrity; }

	UFUNCTION(BlueprintPure, Category = "Ship")
	bool IsSunk() const { return HullIntegrity <= 0.f; }

	EShipAllegiance GetAllegiance() const { return Allegiance; }

	/** The one question the AI asks about another hull. Different sides are
	 *  hostile; the same side is not. A merchant is hostile to both, which is
	 *  what makes her cargo and not a consort - she has no guns to fire back
	 *  with, so "hostile" here costs her nothing and buys the rule its
	 *  simplicity. */
	bool IsHostileTo(const AShipPawn* Other) const
	{
		return Other && Other != this && Other->Allegiance != Allegiance;
	}

	/** She has hauled down her colours: sail furled, helm amidships, and the
	 *  hunters do not look at her again. Damage still lands - a raider who
	 *  keeps firing into a ship that has struck SINKS her, and loses the very
	 *  thing he was firing for. */
	bool HasStruck() const { return bStruck; }

	/** In under the guns of the fort, where nobody can follow her. */
	bool HasMadePort() const { return bMadePort; }

	/** A prize that has reached the roadstead. She is finished: no crew, no
	 *  destination, no more sailing. */
	bool HasLandedAsPrize() const { return bPrizeLanded; }

	/** --- prizes ------------------------------------------------------
	 *
	 *  A ship that has struck is stopped. She is not YOURS until your men are
	 *  aboard her, and those men are gone for the rest of the cruise: they
	 *  are sailing her, not serving your guns. That is the whole cost, and it
	 *  is paid out of the same pool the crew slice made scarce. */
	bool IsPrize() const { return bIsPrize; }
	/** What she was worth at the moment she struck. Set by the game mode
	 *  there, spent by the game mode at the quay. */
	int32 GetPrizeValue() const { return PrizeValue; }
	void SetPrizeValue(int32 Value) { PrizeValue = Value; }
	int32 GetPrizeCrewAboard() const { return PrizeCrewAboard; }
	/** Latched, cumulative: men sent away to prizes over the whole run. */
	int32 GetHandsInPrizes() const { return HandsInPrizes; }
	int32 GetMinHandsAboard() const { return MinHandsAboard; }

	/** How many men she is short FOR GOOD - killed, or in a prize that will
	 *  never come home because it is already landed. Men still at sea in a
	 *  prize are NOT empty berths: they are coming back.
	 *
	 *  It used to be HandsMax - Hands, which counted them, so the port sold
	 *  replacements for men who then returned and the complement ended above
	 *  HandsMax: a review reproduced 72 hands on a 60-berth ship. */
	int32 GetHandsShort() const
	{
		return FMath::Max(0, HandsMax - Hands - GetHandsAway());
	}

	/** Signing on one man in port. Returns true if there was room for him. */
	bool RecruitHand();

	/** --- the magazine ----------------------------------------------------
	 *
	 *  Round shot, and there is only so much of it. FINITE BY DEFAULT, by the
	 *  owner's decision of 16.09: a fight you can lose by running out is a
	 *  different fight, and it is the one this game wants. -Shot=N and
	 *  -EnemyShot=N override the number; either set to 0 makes that ship's
	 *  magazine bottomless again, which is how the old behaviour can still be
	 *  measured against the new one.
	 *
	 *  FORTY, and the number came from the suite rather than from taste. Across
	 *  twenty-four measured scenarios the enemy fires 0 to 20 rounds in almost
	 *  all of them, 24 in a long chase, and 44 in crew_fight - a 360-second
	 *  stand-up duel. Forty is ten broadsides, five to a side: it covers every
	 *  short action outright and empties in exactly the one place a magazine
	 *  ought to start mattering. A larger number would bind nowhere in the
	 *  suite, and a default that binds nowhere cannot be told from no default
	 *  at all. */
	bool HasMagazine() const { return ShotMax > 0; }
	int32 GetShot() const { return Shot; }
	int32 GetShotMax() const { return ShotMax; }
	/** Latched: broadsides refused for want of shot, counted once per spell
	 *  rather than once per tick - the AI asks to fire every frame her guns
	 *  bear, so a per-tick counter would measure the frame rate. */
	int32 GetDryRefusals() const { return DryRefusals; }
	int32 GetShotFired() const { return ShotFired; }

	/** Powder and shot bought in port. Returns true if there was room. */
	bool LoadShot(int32 Rounds);

	/** Timber and tar: hull integrity the SEA cannot give back. Returns how
	 *  much was actually put in, which is less than asked for when she is
	 *  nearly whole. */
	float RepairHull(float Points);

	/** Sends men away to a prize. Refuses, and says so, if it would leave
	 *  fewer than MinHandsAboard to work this ship.
	 *
	 *  It deliberately does NOT go through LoseHands(): men in a prize crew
	 *  are not casualties. Routing them through it to save four lines would
	 *  give one variable two jobs and would move casualties_max - a pinned
	 *  measurement in the convoy family - for a reason that has nothing to do
	 *  with shot. */
	bool DetachPrizeCrew(int32 Count);

	/** The mirror of DetachPrizeCrew: men coming back off a prize that got
	 *  home. Returns how many. One function so that "the men returned" is a
	 *  single fact in a single place - the first version set the pawn's
	 *  counters in one statement and the caller's report in another, and a
	 *  mutation that removed the first left the second saying twelve men came
	 *  home to a ship that never got them. */
	int32 TakeBackPrizeCrew(int32 Count);

	/** Marks her as taken, and puts the prize crew aboard HER. */
	void ManAsPrize(AShipPawn* Taker, int32 CrewAboard);

	/** Who took her, so her crew knows whose deck to come home to. A weak
	 *  pointer: a raider can be sunk while her prize is still running for
	 *  the port, and then the men have no ship to return to - which is the
	 *  truth and is logged rather than papered over. */
	AShipPawn* GetTakenBy() const { return TakenBy.Get(); }

	/** She is in the roadstead. Returns true if this is the landing (it is
	 *  idempotent), and puts into OutReturned the number of men who actually
	 *  reached a deck - which is NOT the same as the number who reached the
	 *  quay: a raider can be sunk while her prize is still running home, and
	 *  then nobody gets those men back.
	 *
	 *  The two were one number until a mutation test removed the return and
	 *  the key called prize_hands_home did not move. It was counting men
	 *  landed WITH a prize, under a name that promised men landed ON A SHIP. */
	bool LandPrize(int32& OutReturned);

	/** Men come home from a prize delivered. Latched and cumulative, like
	 *  HandsInPrizes, so both stay monotonic and the two reconcile:
	 *  Hands + Casualties + (HandsInPrizes - HandsReturned) == HandsMax. */
	int32 GetHandsReturned() const { return HandsReturned; }
	int32 GetHandsAway() const { return HandsInPrizes - HandsReturned; }

	/** The rig threshold that brings a merchant to strike. Read by the game
	 *  mode so the prize log can say WHICH of the two thresholds did it,
	 *  rather than the log guessing at a number the pawn owns. */
	float GetStrikeBelowRig() const { return StrikeBelowRig; }

	/** The one test the hunters make. Sunk, struck or safe in port, she is
	 *  neither a target nor a threat. */
	bool IsOutOfTheFight() const { return IsSunk() || bStruck || bMadePort; }

	/** Hauls down her colours. Idempotent; a sinking ship cannot strike. */
	void Strike(AActor* Causer);

	/** Reached the roadstead. Idempotent. */
	void MakePort();

	/** --- hands ------------------------------------------------------
	 *
	 *  The crew is one pool of men. Shot kills some of them; the rest are
	 *  divided between the guns and the repair parties by RepairShare, and
	 *  the guns reload only as fast as the men left to serve them. This is
	 *  the first slice of crew: sail handling is not yet a station, and
	 *  nothing here can be bought or recruited. */
	int32 GetHands() const { return Hands; }
	int32 GetHandsMax() const { return HandsMax; }
	int32 GetCasualties() const { return Casualties; }
	float GetRepairShare() const { return RepairShare; }
	int32 GetHandsOnRepair() const { return FMath::RoundToInt(Hands * RepairShare); }
	int32 GetHandsOnGuns() const { return Hands - GetHandsOnRepair(); }
	/** How fast the guns reload compared with a full crew: 1 with the guns
	 *  fully manned, down to MinGunCrewFactor with nobody left. */
	float GetGunCrewFactor() const
	{
		return FMath::Clamp(GetHandsOnGuns() / FMath::Max(1.f, FullGunCrew), MinGunCrewFactor, 1.f);
	}
	float GetRepairedTotal() const { return RepairedTotal; }
	/** 0 = every hand at the guns; up to 0.75 = three men in four aloft with
	 *  the carpenter. Logged when it changes. */
	void SetRepairShare(float Share);

	/** True from the moment integrity hit zero until the wreck is destroyed. */
	UFUNCTION(BlueprintPure, Category = "Ship")
	bool IsSinking() const { return SinkPhase != ESinkPhase::Afloat; }

	ESinkPhase GetSinkPhase() const { return SinkPhase; }

	/** True while she is on the bank. */
	UFUNCTION(BlueprintPure, Category = "Ship")
	bool IsAground() const { return bAground; }

	int32 GetGroundForceTicks() const { return GroundForceTicks; }

	/** Which way is out to sea from the ground she is on, horizontal and unit
	 *  length; zero when she is not on any. The captain reads THIS rather than
	 *  walking the islands himself: one copy of the arithmetic, and no actor
	 *  iteration order leaking into a run that has to be byte-reproducible. */
	FVector GetGroundOffshoreDir() const { return GroundOffshore; }

	/** How fast she is still standing further in, in m/s. Zero on the way out,
	 *  which is what makes it a usable signal for "am I still driving myself
	 *  on" rather than "am I moving". */
	float GetGroundClosingMS() const { return GroundClosingMS; }

	/** The furthest into the bank she ever stood. Compared against the
	 *  island's margin, this is the check that the bank is strong enough to
	 *  keep her off the rock - and the log is allowed to contradict it. */
	float GetDeepestPenetrationCm() const { return WorstPenetrationCm; }

	/** Buoyancy actually carried, as a fraction of the weight. 1.0 afloat. */
	UFUNCTION(BlueprintPure, Category = "Ship")
	float GetLiftFraction() const { return LiftFraction; }

	/** Where the killing hit struck, in hull space. */
	FVector GetBreachLocal() const { return BreachLocal; }

	/** How much of her canvas still draws, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetRigEfficiency() const;

	/** The two masts separately: the panel shows them apart, because losing
	 *  the main costs more than losing the fore and a player should see which
	 *  one went. */
	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetForeRigIntegrity() const { return ForeRigIntegrity; }

	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetMainRigIntegrity() const { return MainRigIntegrity; }

	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetRudderIntegrity() const { return RudderIntegrity; }

	/** True while the player is holding the order to point at the enemy rig. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	bool IsAimingHigh() const { return bAimHigh; }

	/** Seconds a side takes to reload, so a gauge can show the fraction. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	float GetReloadSeconds() const { return ReloadSeconds; }

	/** How fast the crew sets or takes in sail, in trim units per second.
	 *  (This comment once described GetNoGoAngleDeg instead, which would have
	 *  had anyone sizing the dead sector from the header shade a quarter of a
	 *  degree of it, with no error anywhere.) */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetSailTrimRate() const { return TrimRate; }

	/** The angle her TRACK makes with the wind, which is the course made good
	 *  and the one that decides whether she is beating. Not derivable by
	 *  subtracting leeway from the wind angle: one is signed and the other is
	 *  not, so that only ever worked on one tack. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetTrackWindAngleDeg() const { return TrackWindAngleDeg; }

	/** How far the crews can train a gun off the beam. Mirroring this number
	 *  in the panel would be a second copy of it. */
	UFUNCTION(BlueprintPure, Category = "Guns")
	float GetMaxTraverseDeg() const { return MaxTraverseDeg; }

	/** Guns still mounted on one side, out of four. */
	UFUNCTION(BlueprintPure, Category = "Damage")
	int32 GetGunsRemaining(bool bStarboard) const;

	UFUNCTION(BlueprintPure, Category = "Ship")
	float GetMaxHullIntegrity() const { return MaxHullIntegrity; }

	/** Top of the HULL in world space. What actually obstructs another ship,
	 *  as opposed to the actor's bounding box, which now includes the rig. */
	float GetHullTopZ() const;

	/** Fatal point damage through the real TakeDamage path, so a scuttled
	 *  ship sinks exactly the way a shot one does. */
	void ScuttleHull(bool bStarboard, AActor* Causer);

	/** Console command for the editor: `Scuttle` sinks this ship. */
	UFUNCTION(Exec)
	void Scuttle();

	FOnShipSunk OnShipSunk;
	FOnShipWrecked OnShipWrecked;
	FOnShipStruck OnShipStruck;

	/** Drive inputs, so an AI controller can sail the same hull a player does.
	 *  Trim is a rate: +1 sets more sail, -1 takes it in. */
	UFUNCTION(BlueprintCallable, Category = "Sailing")
	void SetSailTrimInput(float Value) { if (!IsSinking()) SailTrimInput = FMath::Clamp(Value, -1.f, 1.f); }

	UFUNCTION(BlueprintCallable, Category = "Sailing")
	void SetSteerInput(float Value) { if (!IsSinking()) SteerInput = FMath::Clamp(Value, -1.f, 1.f); }

	/** Closest the rig can point to the wind, so an AI can avoid the no-go zone. */
	UFUNCTION(BlueprintPure, Category = "Sailing")
	float GetNoGoAngleDeg() const { return NoGoAngleDeg; }

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator, AActor* DamageCauser) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UBoxComponent> HullCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	/** One dynamic material per slot of the hull mesh, made on the first tick.
	 *  They exist to carry the wind into the rig: the master bends masts, sails
	 *  and cordage by SwayAmount, and until something pushed a wind speed into
	 *  it that parameter was a knob nobody turned - the same shape of defect as
	 *  the three dead wake knobs, and just as invisible. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RigMaterials;

	bool bRigSwayReported = false;

	/** EXPERIMENT: does a query-only child box weld into the simulating hull
	 *  and change its inertia? Measured, not assumed. */
	/** The fore and main rigs, as things a shot can find. QUERY ONLY, which
	 *  is a hard guarantee and not a preference: UShapeComponent's constructor
	 *  turns auto-welding on, and every path that could weld a child into the
	 *  simulating hull tests the collision-enabled type first and lets
	 *  QueryOnly through untouched. Measured: mass, centre of mass and the
	 *  inertia tensor are bit-identical with these attached. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UBoxComponent> ForeRig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UBoxComponent> MainRig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UBuoyancyComponent> Buoyancy;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UCameraComponent> ShipCamera;

	// ---- hull ----------------------------------------------------------
	/** Displacement in kilograms. */
	UPROPERTY(EditAnywhere, Category = "Sailing")
	float ShipMassKg = 60000.f;

	/** Drive at full sail, best angle and reference wind, in kg*cm/s^2. */
	UPROPERTY(EditAnywhere, Category = "Sailing")
	float MaxSailForce = 1.3e8f;

	/** Peak yaw torque from the rudder, in kg*cm^2/s^2. Raised with the keel:
	 *  a lateral plane resists turning as well as sideslip, and at 2.0e10 the
	 *  full-rudder rate fell from 7.1 to 5.0 degrees a second. Set from
	 *  measurement, not arithmetic: see -ShipRudderTest. */
	UPROPERTY(EditAnywhere, Category = "Sailing")
	float TurnTorque = 3.3e10f;

	/** A rudder only bites when water is moving past it. Speed in cm/s at
	 *  which steering reaches full authority. */
	UPROPERTY(EditAnywhere, Category = "Sailing")
	float FullSteeringSpeed = 400.f;

	/** Hull speed limit in cm/s. */
	UPROPERTY(EditAnywhere, Category = "Sailing")
	float MaxForwardSpeed = 800.f;

	// ---- rig -----------------------------------------------------------
	/** Closest a square rig can point to the wind. Inside this the sails flog
	 *  and the ship makes no way at all. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float NoGoAngleDeg = 48.f;

	/** How fast the crew sets or takes in sail, in trim units per second. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float TrimRate = 0.25f;

	/** Wind speed the drive numbers are calibrated against, in m/s. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float ReferenceWindMS = 9.f;

	/** Sideways push on the rig, as a fraction of forward drive. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float LeewayFraction = 0.18f;

	// ---- lateral plane (the keel) --------------------------------------
	/** Side force per unit of (flow speed x sideslip speed), per panel, in
	 *  kg/cm. A hull at a leeway angle is a very low aspect ratio foil: its
	 *  lift and its induced drag, resolved into hull axes, cancel exactly
	 *  along the hull and leave a pure side force of 0.5*rho*A*a*V*v. So a
	 *  keel modelled this way costs no forward speed at all, which is both
	 *  what the flat plate identity says and the cleanest guarantee that it
	 *  cannot quietly break the speed calibration.
	 *  0.5 * 1.025e-3 kg/cm3 * 3.9e5 cm2 * 0.45 = 90. The area is half the
	 *  lateral plane (31 m of hull by 3.35 m of immersed depth, three
	 *  quarters full), and 0.45 is the slender-body lift slope at the plane's
	 *  effective aspect ratio of 0.29, doubled for the mirror in the surface. */
	UPROPERTY(EditAnywhere, Category = "Keel")
	float PanelLiftK = 90.f;

	/** Side force per unit of sideslip speed squared, per panel, in kg/cm.
	 *  The lift term needs way on; a ship with none is simply being blown
	 *  sideways, and this bluff-body term is what stops that running away.
	 *  0.5 * 1.025e-3 * 3.9e5 * 0.90, the drag coefficient of a full hull
	 *  dragged broadside. The two terms cross over at 30 degrees of leeway. */
	UPROPERTY(EditAnywhere, Category = "Keel")
	float PanelCrossK = 180.f;

	/** Centre of lateral resistance, in hull space, along the hull. ABAFT the
	 *  centre of mass on purpose: that is what makes sideslip resistance into
	 *  directional stability. Forward of it and the hull will not hold a
	 *  course, the way a weather vane pinned at its nose will not. */
	UPROPERTY(EditAnywhere, Category = "Keel")
	float LateralCentreXCm = -120.f;

	/** Half the spacing of the two panels the plane is split into. The plane's
	 *  own radius of gyration, 3100 / sqrt(12): resisting sideslip needs only
	 *  the centroid, but resisting YAW needs the second moment, and a single
	 *  point at the centre of lateral resistance would deliver about one
	 *  fiftieth of the right yaw damping. */
	UPROPERTY(EditAnywhere, Category = "Keel")
	float LateralGyradiusCm = 895.f;

	/** --- the ground: what shallow water does to a hull -------------------
	 *
	 *  She is never resolved against rock (AIsland refuses ECC_Pawn), so this
	 *  is the ONLY thing land does to a hull. It is a spring that pushes her
	 *  offshore, growing with how far into the bank she stands, plus a damper
	 *  that acts only while she is still standing further in. Both are
	 *  directed out to sea, which means no value of either can hold a ship who
	 *  is leaving: the mechanism can cost her way, and nothing else.
	 *
	 *  Sized against the measured hull: 60 000 kg entering the bank at 7 m/s
	 *  is brought up in v*sqrt(m/k) = 20 m, well inside the 50 m of bank. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float ShoalPushK = 7400.f;

	/** The damper: the sand eating her way. Proportional to how fast she is
	 *  still standing further in, and NOT to how deep she is - measured, a
	 *  depth-proportional damper bites only once she is well in, by which time
	 *  the spring has already taken most of the speed and there is little left
	 *  to eat, and she came off stern-first at 5.3 m/s. Sized on the hull: at
	 *  60 000 kg this is a time constant of half a second. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float ShoalDragK = 120000.f;

	/** ...ramped in over the first few metres of bank, so the edge of the
	 *  shallows is a shelving beach and not a kerb. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float ShoalDragRampCm = 400.f;

	/** Ceiling on the ground force, as a multiple of the ship's weight. The
	 *  bank stiffens without limit towards the beach, so this exists only to
	 *  keep a number finite for the solver; at twenty times her weight it is a
	 *  hundred times anything the sails can push with, and it is logged if it
	 *  is ever reached. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float MaxGroundForceWeights = 20.f;

	/** How far in she has to be before she is AGROUND rather than brushing
	 *  the edge of the shallows. Measured without it: a hull lying exactly on
	 *  the line went aground and afloat and aground again as the swell moved
	 *  her, and each rising edge was a fresh wound. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float ShoalBiteCm = 50.f;

	/** Hull damage for each m/s she is making when she first feels the
	 *  ground. Touching gently is cheap; driving her on at six knots costs a
	 *  quarter of the hull and can, through the ordinary flooding model, sink
	 *  her where she lies. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float GroundingDamagePerMS = 45.f;

	/** How long she must be clear of the bank before she counts as off again.
	 *  In TIME, not in distance: the first version wanted five metres of
	 *  offing, which a ship pushed out by a spring can never have, because the
	 *  spring stops pushing at the exact moment she reaches the edge. She lay
	 *  there for 140 seconds, off the ground by every measure, and never once
	 *  said so. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float ShoalReleaseSeconds = 3.f;

	/** Rudder authority a ship keeps with no way on, as long as sail is set.
	 *  A square rig caught in irons backs its sails to pay off; without this a
	 *  ship that stops head to wind can neither move nor turn, ever. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float BackedSailAuthority = 0.22f;

	/** Roll torque from wind pressure on the beam, in kg*cm^2/s^2. */
	UPROPERTY(EditAnywhere, Category = "Rig")
	float HeelTorque = 2.2e9f;

	// ---- guns ----------------------------------------------------------
	/** What the guns throw. Defaults to ACannonBall. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	TSubclassOf<ACannonBall> CannonBallClass;

	/** Muzzle velocity in metres per second. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float MuzzleVelocityMS = 150.f;

	/** Barrel elevation above horizontal, in degrees. This and the muzzle
	 *  velocity together set the range. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float GunElevationDeg = 6.f;

	/** How long a gun crew takes to sponge, load and run out again. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float ReloadSeconds = 12.f;

	/** How far the crews can train a gun off the beam, in degrees. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float MaxTraverseDeg = 12.f;

	/** Shots fall short of the ideal arc by drag, so the crews lay a little
	 *  longer than the vacuum solution. Measured at 1.08 the mean point of
	 *  impact stood 285 cm above the target's waterline, on her rail rather
	 *  than her side, and the gun deck was never touched at all; 1.04 puts it
	 *  back on the strake where the ports are. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float RangeBias = 1.04f;

	/** --- the two things a gun on a MOVING platform has to know -----------
	 *
	 *  Round shot leaves the muzzle carrying the ship's own way. It always did
	 *  in the world; it did not in this project, where the ball was launched
	 *  with `Aim * MuzzleSpeed` and nothing else. At a hundred and forty-five
	 *  metres a second the shot is in the air two and a half seconds at long
	 *  range, and a ship making six and a half metres a second travels sixteen
	 *  metres in that time - more than half her own length, and sideways to the
	 *  line of fire, because guns fire on the beam.
	 *
	 *  And the target is doing the same thing. Laying the guns on where she IS
	 *  is laying them on where she WAS by the time the shot arrives.
	 *
	 *  The two belong together. Adding the inherited velocity alone would make
	 *  a moving ship shoot worse, not better, which is why both are here and
	 *  why the lead is computed on the RELATIVE velocity: in the frame of the
	 *  firing ship the ball leaves at the muzzle velocity and the target drifts
	 *  at (target - self), so that is the only speed the aim has to allow for.
	 *
	 *  Both default ON. -ShipInheritVel=0 and -ShipLead=0 restore the old
	 *  behaviour exactly, which is what makes the change measurable rather than
	 *  merely argued. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	bool bInheritShipVelocity = true;

	UPROPERTY(EditAnywhere, Category = "Guns")
	bool bLeadTarget = true;

	/** Gun smoke. OFF unless -ShipSmoke=1. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	bool bGunSmoke = false;

	/** How many times the lead is refined. The flight time depends on the
	 *  range and the range depends on the lead, so one pass is already close
	 *  and two is inside a metre at any range these guns can reach. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	int32 LeadPasses = 2;

	/** Random scatter per gun in traverse, in degrees. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float SpreadDeg = 1.6f;

	/** Random scatter per gun in elevation, in degrees. Kept far tighter than
	 *  traverse: on this arc one degree of elevation is ~80 m of range, so a
	 *  1.6 deg cone put a broadside anywhere across 260 m. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float ElevationSpreadDeg = 0.35f;

	/** Sideways kick the hull takes per gun fired, in kg*cm/s. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float RecoilImpulse = 9.0e5f;

	/** Hull integrity when undamaged. */
	UPROPERTY(EditAnywhere, Category = "Ship")
	float MaxHullIntegrity = 1000.f;

	/** Player on this class, Crown on AEnemyShipPawn, and later Merchant on the
	 *  convoy. Set by the CLASS rather than at runtime, so there is no window in
	 *  which a hull has no side and nothing to go wrong on possession. */
	UPROPERTY(EditDefaultsOnly, Category = "Ship")
	EShipAllegiance Allegiance = EShipAllegiance::Player;

	/** A merchant strikes when her hull is down to this fraction, or when her
	 *  rig is down to StrikeBelowRig. She is nobody's man-of-war: a master
	 *  who lets his ship be shot to pieces for somebody else's cargo is not
	 *  a master for long, and a laden hull at seven knots that has lost four
	 *  sails to a frigate's broadside is not going to outrun anything. The
	 *  rig test is the one that fires in practice, because the hunters fire
	 *  HIGH first - cripple her, then sink her - and a rig hit costs 0.12 of
	 *  a mast, so 0.6 is about five balls aloft. Only the Merchant side ever
	 *  strikes; a fighting ship sinks. */
	UPROPERTY(EditDefaultsOnly, Category = "Ship")
	float StrikeBelowFraction = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Ship")
	float StrikeBelowRig = 0.6f;

	/** Men aboard when she sails. Sixty is a small privateer's company; a
	 *  merchant carries a dozen and says so in her own constructor. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	int32 HandsMax = 60;

	/** Men lost to one ball in each zone. A hull hit sends splinters through
	 *  a crowded deck; a gun hit kills the crew of that gun; aloft there is
	 *  nobody much to kill. Grounding blows kill nobody. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	int32 HullHitCasualties = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	int32 GunHitCasualties = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	int32 RigHitCasualties = 1;

	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	int32 RudderHitCasualties = 1;

	/** Men it takes to serve every gun at full speed: six to a gun, eight
	 *  guns. With sixty aboard there are twelve to spare, so the first dozen
	 *  casualties cost nothing at the guns and every one after that slows
	 *  the reload. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	float FullGunCrew = 48.f;

	/** The guns never stop entirely: one man can load a gun, slowly. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	float MinGunCrewFactor = 0.25f;

	/** Integrity restored per man per second. 0.00015 puts thirty hands at
	 *  0.0045 a second: one rig hit (0.12) knotted and spliced in about
	 *  twenty-five seconds, a mast from 0.40 back to the jury cap in a
	 *  hundred. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	float RepairPerHandPerSecond = 0.00015f;

	/** A splice never makes a mast whole. Repairs at sea stop here; what
	 *  was above it stays where it is. */
	UPROPERTY(EditDefaultsOnly, Category = "Crew")
	float JuryCap = 0.85f;

	/** Fewest men that can still work this ship. A captain who put every
	 *  hand into prizes would be a passenger on his own deck. */
	UPROPERTY(EditDefaultsOnly, Category = "Prize")
	int32 MinHandsAboard = 20;

	/** Rounds in the magazine. ZERO MEANS UNLIMITED - see HasMagazine. */
	UPROPERTY(EditDefaultsOnly, Category = "Magazine")
	int32 ShotMax = 40;

	// ---- damage by zone ------------------------------------------------
	/** Fraction of a mast's rig carried away by one ball. Three hits and a
	 *  mast is a bare pole: round shot cuts shrouds and halyards, and a sail
	 *  whose yard has come down draws nothing whatever its cloth is like.
	 *  At 0.34 two broadsides stripped a ship bare in forty seconds, which is
	 *  no kind of fight; eight balls into one mast is an investment. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float RigHitDamage = 0.12f;

	/** Share of the drive each mast carries. The main is four metres taller
	 *  and spreads the larger courses. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float ForeRigShare = 0.45f;

	/** What one ball does to the steering. Two through the transom and the
	 *  rudder is gone. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float RudderHitDamage = 0.5f;

	/** Steering left when the rudder is shot away. Not zero: a crew can still
	 *  coax her round by bracing the yards, slowly and badly. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float RudderlessAuthority = 0.15f;

	/** A shot lands in the battery if it comes in this close to a gun port
	 *  abscissa, this far out on the beam, and within the gun deck's height,
	 *  which is the band GunDeckLowCm to GunDeckHighCm about the ports
	 *  modelled at +120. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float GunPortWindowCm = 100.f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	float GunDeckLowCm = 0.f;

	UPROPERTY(EditAnywhere, Category = "Damage")
	float GunDeckHighCm = 240.f;

	/** Where the crews point when told to fire high: up at the main top. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float HighAimHeightCm = 1250.f;

	/** And how far along her, because the main mast is not amidships. Aiming
	 *  at her centreline put every high shot through the 4 m gap BETWEEN the
	 *  two masts: fore rig spans X 330 to 790 and main -550 to -90, and the
	 *  aim point sat at 0. Every rig hit in the logs so far was scatter
	 *  carrying a ball onto a mast that was never aimed at. */
	UPROPERTY(EditAnywhere, Category = "Damage")
	float HighAimAlongCm = -320.f;

	// ---- sinking -------------------------------------------------------
	/** From rest draught to deck awash. Long enough to read the list and trim
	 *  once a second, short enough that a fight ends inside a minute. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodSeconds = 25.f;

	/** Draught (hull origin below the local sea surface) at which the weather
	 *  deck meets the water. Origin on the waterline, box half-height 350. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float AwashDraughtCm = -250.f;

	/** How far below the hull origin the buoyancy spheres sit. The origin is the
	 *  waterline the hull was drawn around; spheres centred on it can only
	 *  balance the weight with the origin submerged, which floated the ship a
	 *  metre low and put her gun ports at the sea's surface. Calibrated by
	 *  measurement against the resting z, not assumed. */
	UPROPERTY(EditAnywhere, Category = "Sea")
	float PontoonDropCm = 80.f;

	/** If the draught profile is not reached by FloodSeconds plus this, she
	 *  founders anyway: a stall is a log line, never a hang. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float AwashFailsafeSeconds = 10.f;

	/** Lift removed per second while she is still riding higher than the
	 *  profile wants. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodRateBase = 0.02f;

	/** Lift removed per second while she is already DEEPER than the profile.
	 *  Water never leaves a holed hull, so this is never zero, but it has to
	 *  be small or the profile governs nothing: at 0.02 the flood ran at the
	 *  base rate from end to end (the hull is pushed under by the flood water
	 *  long before the profile asks for it), the deck went awash in 12 s
	 *  whatever FloodSeconds said, and FloodRateGain never once applied. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodTrickleRate = 0.004f;

	/** Extra lift removal per second per cm she lags the draught profile. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodRateGain = 0.0008f;

	/** Cap on that rate, so a one-frame-old draught cannot make it chatter. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodRateMax = 0.08f;

	/** Flood water at full flooding, as a fraction of the ship's weight. Its
	 *  roll arm is three times the sail heel torque, so the list is unmistakable.
	 *  It also sets the plunge speed, since the water stays aboard. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodWaterFraction = 0.30f;

	/** Where the flood water pools: bilge level, on the breach side. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodCentreY = 300.f;

	UPROPERTY(EditAnywhere, Category = "Sinking")
	float FloodCentreZ = -200.f;

	/** The two pontoons nearest the breach lose all their lift at full
	 *  flooding; the other four lose this much. The asymmetry is what makes
	 *  her list toward the hit and go down by that end. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float HoledWeight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Sinking")
	float LeakingWeight = 0.75f;

	/** Dwell with the deck awash before the plunge, so the moment reads. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float HangSeconds = 5.f;

	/** The remaining lift is run to zero over this long. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float PlungeSeconds = 6.f;

	/** Body damping during the plunge: the only vertical brake once lift is
	 *  gone (buoyancy damping is upward-only, water drag is XY-only). The
	 *  flood water is still aboard, so the terminal speed is
	 *  (1 + FloodWaterFraction) * 980 / this = 2.55 m/s. Measured: 2.54. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float SinkLinearDamping = 5.f;

	/** Draught at which the wreck is destroyed: mast tops (+2510) are under
	 *  and the box top (-2650) is below any fresh hull's bottom (-405). */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float WreckDepthCm = -3000.f;

	/** Wreck regardless after this long plunging. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	float PlungeFailsafeSeconds = 60.f;

	/** Starboard bow at the waterline: the breach used by the scuttle test and
	 *  the fallback when no point-damage hit was recorded. */
	UPROPERTY(EditAnywhere, Category = "Sinking")
	FVector ScuttleBreachLocal = FVector(900.f, 520.f, -100.f);

	// ---- diagnostics ---------------------------------------------------
	/** Writes one SHIPLOG line per second so behaviour can be measured from a
	 *  headless run instead of guessed at. */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	bool bLogFloatState = true;

private:
	/** Overlap events are the only thing that normally switches buoyancy on.
	 *  For an open ocean that is fragile, so we register the water bodies
	 *  explicitly at BeginPlay instead. */
	int32 RegisterWaterBodies();

	/** Runs a moment after BeginPlay so ABuoyancyManager certainly exists by
	 *  the time we register with it. */
	UFUNCTION()
	void DeferredWaterRegistration();

	FTimerHandle WaterRegistrationTimer;

	UWindSubsystem* GetWind() const;

	UPROPERTY(Transient)
	TObjectPtr<UWaterBodyComponent> PrimaryWaterBody;

	UFUNCTION()
	void OnHullHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	float LastHullHitLogTime = -10.f;

	/** Which zone a hit belongs to, and which gun if it was the battery. */
	EShipZone ClassifyHit(const UPrimitiveComponent* Struck,
		const FVector& HullLocal, int32& OutGun) const;

	void OnSailTrimInput(float Value);
	void OnAimHighPressed();
	void OnAimHighReleased();
	void OnSteerRate(float Value);
	void OnFirePort();
	void OnFireStarboard();
	void OnSteer(float Value);
	void OnTurnCamera(float Value);

	/** Puts the boom on a named, repeatable vantage for a gallery run. */
	void ApplyShotCamera();
	void OnLookUp(float Value);

	/** Seconds after BeginPlay to fire a screenshot, from -ShipShotAfter=N.
	 *  A startup HighResShot always lands on frame 5, before the world has
	 *  drawn anything, so the trigger has to come from inside the running game. */
	float ShotAfterSeconds = 0.f;

	/** --- the gallery -----------------------------------------------------
	 *
	 *  A graphics pass cannot be judged by a log line, and it cannot be judged
	 *  by one screenshot either: one frame of a moving sea taken at whatever
	 *  moment the run happened to reach is not a measurement, it is an
	 *  anecdote. -ShipShots=5,20,60 takes SEVERAL, at times I choose, named by
	 *  the time, so the same three frames can be put side by side before and
	 *  after a change. With -UseFixedTimeStep -FPS=60 -ShipSeed=1 the world is
	 *  in the same state at t=20 in every run, so the only thing that differs
	 *  between two galleries is the rendering. */
	TArray<float> ShotTimes;
	TArray<bool> ShotTaken;

	/** Prefix for the files, so two galleries can live side by side. */
	FString ShotName = TEXT("Shot");

	/** A named vantage, applied at BeginPlay, that takes the boom off the
	 *  controller and frames the hull the same way every run. Without it the
	 *  camera points wherever the controller's rotation happens to be and the
	 *  "same" frame is a different picture each build. */
	FString ShotCam;
	/** -ShipRudderTest=N pins full sail and full rudder from N seconds, so the
	 *  steady turn rate can be read straight out of the yaw column. */
	float RudderTestAt = 0.f;

	/** -ShipWindSweep=N walks the wind right around the compass over N seconds
	 *  with every sail set, which prints a polar diagram into the log.
	 *
	 *  It is a TRANSIENT and the table it produced should not be trusted: the
	 *  wind turns two degrees a second while the ship is still accelerating,
	 *  and her head is free, so the wind angle in each row moves for two
	 *  unrelated reasons at once. The same build gave 60 degrees -> 3.96 m/s
	 *  with a fixed timestep and 3.27 without. Use -ShipPolar instead. */
	float WindSweepSeconds = 0.f;

	/** -ShipPolar=1 measures the polar properly: the wind is pinned, the ship
	 *  is steered onto a heading and HELD there until she stops changing, and
	 *  only then is the row written. One row per heading, each one a steady
	 *  state rather than a photograph of a ship still speeding up. */
	bool bPolarTest = false;

	/** Degrees between rows. */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	float PolarStepDeg = 5.f;

	/** Longest she is given to settle on one heading before the row is written
	 *  anyway, marked as not settled. */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	float PolarDwellSeconds = 40.f;

	/** Settled means her speed has moved less than this, in m/s, across a
	 *  whole second. */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	float PolarSettledMS = 0.005f;

	float PolarTargetYaw = 0.f;
	float PolarDwellElapsed = 0.f;
	float PolarLastSpeed = -1.f;
	float PolarSettleTimer = 0.f;
	int32 PolarRow = 0;
	int32 PolarQuietSeconds = 0;
	bool bPolarRowDue = false;
	bool bPolarRowSettled = false;

	/** -ShipRunAground=N steers the ship at the nearest island under full sail
	 *  from N seconds, then furls ten seconds after she strikes. A run that
	 *  contains no grounding is a FAILED run, not a clean one: without this an
	 *  island slice can come back green having never been touched. */
	float RunAgroundAt = 0.f;
	bool bRunAgroundFurled = false;

	float PlayTime = 0.f;
	/** Metres of lead applied to the last broadside, for the log. Counted, not
	 *  inferred: a lead of zero in a run that asked for one is the difference
	 *  between "the feature is off" and "the target was not moving". */
	float LastLeadCm = 0.f;
	bool bShotTaken = false;
	bool bBuoyancyDumped = false;

	/** -ShipFireTest=N lets go a starboard broadside at N seconds so the range
	 *  can be read out of the log without anyone touching a key. */
	float FireTestAt = 0.f;
	bool bFireTestDone = false;

	float PortReload = 0.f;
	float StarboardReload = 0.f;
	float HullIntegrity = 1000.f;
	int32 ShotCounter = 0;

	// ---- sinking state -------------------------------------------------
	void BeginSinking(AActor* Causer);
	/** Returns true when the wreck destroyed itself; the caller must return. */
	bool TickSinking(float DeltaSeconds);
	void EnterFoundering(const TCHAR* Reason);
	void EnterPlunging();
	void ApplyLiftScales();
	float ComputeLift();
	/** Local sea surface height: the mean water height under the wet pontoons. */
	float WaterSurfaceZ() const;

	ESinkPhase SinkPhase = ESinkPhase::Afloat;
	float SinkTime = 0.f;
	float PhaseTime = 0.f;
	float FloodScale = 1.f;
	float PlungeScale = 1.f;
	float RestDraught = 0.f;
	float RestCoefficient[6] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	float BreachWeight[6] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	FVector LastHitLocal = FVector::ZeroVector;
	bool bHasLastHit = false;
	FVector BreachLocal = FVector::ZeroVector;
	float LiftFraction = 0.f;

	float SailTrim = 0.f;
	float SailTrimInput = 0.f;
	/** Two axes reach the helm, the keys and the arrows, and they are summed.
	 *  Binding both to one setter meant whichever ran last won, so the arrows
	 *  overwrote A and D with zero every frame and the keys did nothing. */
	float SteerInput = 0.f;
	float SteerRateInput = 0.f;

	// ---- damage by zone ------------------------------------------------
	float ForeRigIntegrity = 1.f;
	float MainRigIntegrity = 1.f;
	float RudderIntegrity = 1.f;
	/** [0] port, [1] starboard; true means that gun is dismounted. */
	bool bGunDown[2][4] = { { false, false, false, false }, { false, false, false, false } };
	/** True only inside a scuttle, so a test sinking is never reclassified
	 *  as a rig or rudder hit. */
	bool bScuttling = false;

	/** Set once by Strike() / MakePort(). Never cleared: a ship that has
	 *  struck stays struck for the rest of the run. */
	bool bStruck = false;
	bool bMadePort = false;

	int32 Hands = 0;
	/** Latched. Never goes down. */
	int32 Casualties = 0;
	/** Also latched, and SEPARATE from Casualties on purpose - see
	 *  DetachPrizeCrew. Note the consequence, written down rather than
	 *  discovered later: once a prize crew has left, Hands + Casualties no
	 *  longer equals HandsMax. The missing men are here. */
	int32 HandsInPrizes = 0;
	int32 HandsReturned = 0;
	int32 Shot = 0;
	/** Both latched, both cumulative. */
	int32 ShotFired = 0;
	int32 DryRefusals = 0;
	/** True while she has already been refused for this dry spell, so the
	 *  counter counts spells and not frames. Cleared when she fires. */
	bool bReportedDry = false;
	bool bIsPrize = false;
	bool bPrizeLanded = false;
	int32 PrizeValue = 0;
	int32 PrizeCrewAboard = 0;
	TWeakObjectPtr<AShipPawn> TakenBy;
	float RepairShare = 0.f;
	/** Latched: integrity restored over the whole run, all targets. */
	float RepairedTotal = 0.f;
	bool bRepairsLogged = false;

	void LoseHands(int32 Count, const TCHAR* Why);
	void TickRepairs(float DeltaSeconds);
	void OnRepairPressed();
	/** True only while a grounding wound is being delivered, so its log lines
	 *  are tagged GROUNDLOG and never counted as gunnery. */
	bool bGroundingBlow = false;
	/** Held by the player to point at the enemy's rig instead of her hull. */
	bool bAimHigh = false;

	// ---- laying the guns by hand, state ----------------------------------
	bool bLayingByHand = false;
	bool bLayStarboard = true;
	bool bLayLocked = false;
	bool bAgainstStop = false;
	float LayTrainDeg = 0.f;
	/** Three degrees at rest: point blank for these guns is about 190 m, which
	 *  is inside the range every action in this game is fought at, so the ship
	 *  starts laid at something usable rather than at zero. */
	float LayElevationDeg = 3.f;

	/** How far the wheel moves the barrels per notch. A whole degree a notch
	 *  makes the wheel unusable at long range, where a degree is forty metres
	 *  of fall of shot; a fifth of that is a metre of elevation quoin, which is
	 *  about what a crew could actually set. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float ElevationPerNotchDeg = 0.2f;

	/** The carriage's elevation stops. Ten degrees up is what a truck carriage
	 *  on a quoin allows; three down is the roll she can be fired on. These
	 *  bound the PLAYER's wheel only - the AI's solver is untouched, so no
	 *  measured number can move through them. */
	UPROPERTY(EditAnywhere, Category = "Guns")
	float MaxLayElevationDeg = 10.f;

	UPROPERTY(EditAnywhere, Category = "Guns")
	float MinLayElevationDeg = -3.f;

	/** -LayTrain= and -LayElev= pin the guns without a mouse, so the feature can
	 *  be measured and photographed headlessly at all. Without them the only way
	 *  to see the marks would be to play the game by hand, and a visual feature
	 *  nobody can capture is one nobody can review. */
	bool bLayPinned = false;

	void UpdateGunLaying();
	void OnElevate(float Value);
	void OnLayLockPressed();
	/** Where a shot ordered high should be laid on another ship: at her main
	 *  top, not at the air between her masts. */
	FVector HighAimPoint() const;

	/** The hull's lateral plane: what a keel does. */
	void ApplyLateralResistance();

	/** The bank under her: what shallow water does. */
	void ApplyGroundContact();

	/** How far along her the bank is sampled, forward and aft of the centre of
	 *  mass. The hull box is 1550 cm each way and the first version sampled
	 *  ONE point, her middle - so a bank 5000 cm wide was really 3450 cm wide,
	 *  and a ship held against it under sail came to rest with her bow four
	 *  metres inside the hillside while every reading stayed green. */
	UPROPERTY(EditAnywhere, Category = "Grounding")
	float GroundProbeAlongCm = 1550.f;

	bool bAground = false;
	float AgroundSince = 0.f;
	float ClearSince = 0.f;
	float AgroundEntrySpeed = 0.f;
	/** How far into the bank she is standing RIGHT NOW. On the SHIPLOG line,
	 *  because a hull sitting motionless near a shore with no such column is a
	 *  hull whose state has to be guessed at - which is how the last two
	 *  defects in this project survived as long as they did. */
	float PenetrationCm = 0.f;
	FVector GroundOffshore = FVector::ZeroVector;
	float GroundClosingMS = 0.f;
	float DeepestPenetrationCm = 0.f;
	/** The same, but never cleared: what the whole run is judged on. */
	float WorstPenetrationCm = 0.f;
	bool bWarnedForceCapped = false;
	bool bWarnedOnRock = false;
	/** Counted, not assumed. A run with no island must end with this at zero,
	 *  which is a measurement; "the force is negligible out there" is not. */
	int32 GroundForceTicks = 0;

	float WindAngleDeg = 0.f;
	float LeewayDeg = 0.f;
	/** Angle between the TRACK and where the wind comes from, in degrees.
	 *  This is the course made good, and it is the one that decides whether
	 *  she is beating or merely pointing. */
	float TrackWindAngleDeg = 0.f;
	float LastYaw = 0.f;
	bool bHasLastYaw = false;
	float YawRateDegPerSec = 0.f;
	float WindwardVMG = 0.f;
	float LastDrive = 0.f;
	float SweepLogTimer = 0.f;
	float LogTimer = 0.f;
};
