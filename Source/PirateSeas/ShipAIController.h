#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ShipAIController.generated.h"

class AShipPawn;

UENUM()
enum class EShipTactic : uint8
{
	/** Too far to shoot. Point the bow at her and pile on sail. */
	Close,
	/** In range. Turn to lay the guns on and hold the distance. */
	Engage,
	/** Badly hurt. Put the wind astern and run. */
	Disengage
};

/**
 * Sails an enemy ship the way a captain would, not the way a homing missile
 * would: it cannot steer into the no-go zone, so when the course it wants lies
 * inside the wind it picks the nearer tack instead.
 */
UCLASS()
class PIRATESEAS_API AShipAIController : public AAIController
{
	GENERATED_BODY()

public:
	AShipAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Where a merchant is bound. The game mode lays the course; she sails it. */
	void SetDestination(const FVector& Where);

	/** Counted, not assumed: both must read 0 in a world with no island. */
	int32 GetLandTicks() const { return LandTicks; }
	int32 GetClawOffs() const { return ClawOffs; }
	int32 GetRejoinTicks() const { return RejoinTicks; }
	int32 GetWears() const { return Wears; }
	int32 GetAvoidTicks() const { return AvoidTicks; }
	int32 GetTacksThrough() const { return TacksThrough; }
	/** Ticks spent running down a chase instead of laying the guns. Zero
	 *  against anything that does not make off. */
	int32 GetPursuitTicks() const { return PursuitTicks; }

protected:
	/** Beyond this the enemy just closes the distance. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float EngageRangeM = 550.f;

	/** Distance she tries to hold once alongside. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float StandoffM = 320.f;

	/** How far off the beam the target may sit before she fires. Must stay
	 *  INSIDE the guns' traverse (12 deg): at the edge of a wider arc the
	 *  carriages cannot finish training and every shot lands 20 m to one side
	 *  of a hull that is only 15 m half-long. Measured: 0 hits from 8. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float FiringArcDeg = 9.f;

	/** Below this fraction of hull she breaks off. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float DisengageHullFraction = 0.3f;

	/** Degrees of heading error that call for full rudder. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float FullRudderErrorDeg = 25.f;

	/** How far outside the no-go zone a close-hauled course is laid. At the
	 *  very edge the rig gives almost nothing; this puts her where it draws. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float CloseHauledMarginDeg = 22.f;

	/** Once on a tack, hold it at least this long. Without it every wander of
	 *  the wind flipped the tack before she had made any way at all. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float TackHoldSeconds = 45.f;

	/** Heading error, in degrees, at which a wear is considered finished even
	 *  if the geometry still says the short way crosses the wind. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float WearDoneErrorDeg = 8.f;

	/** Way on, in m/s, above which she TACKS through the wind instead of
	 *  wearing round through downwind.
	 *
	 *  The wear rule was written to stop her stalling head to wind, and its own
	 *  comment says why: "a square rig only crosses the eye of the wind with
	 *  real way on". But it was applied with no speed test at all, so she wore
	 *  every time she changed board, including at six knots when she could have
	 *  come about in seconds. Measured with it unconditional, beating 600 m up
	 *  to a target: she gained about 40 m on each board and lost 110 m on each
	 *  wear, so in four hundred seconds she finished 285 m FURTHER AWAY than
	 *  she started and never fired a shot.
	 *
	 *  Four metres a second, because her close-hauled speed is 4.89 and a ship
	 *  slower than this really has not got the way on. A very large value
	 *  restores the old behaviour exactly, which is what made the change
	 *  measurable rather than merely argued: wearing always / tacking above
	 *  4.0, on the same seed, gave 3 broadsides against 7 in the standard
	 *  fight, 5 against 6 with a squadron, and 4 hits against 13.
	 *
	 *  It is not free. Her longest spell caught head to wind grows from about
	 *  6 seconds to about 40: tacking sometimes fails and she has to pay for
	 *  it. That is the trade the old unconditional rule was avoiding, and it
	 *  is worth making, because a wear costs a hundred metres to leeward EVERY
	 *  time and a failed tack costs forty seconds once. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float TackAboveMS = 4.f;

	/** Interval between ships in the line, in centimetres. 120 m is four ship
	 *  lengths: the gap between hulls is then 89 m, which at 6.6 m/s gives a
	 *  follower thirteen seconds to answer a next ahead who stops dead, and
	 *  three ships span 240 m, comfortably inside the 550 m at which the whole
	 *  line can engage one target. The probe's 250 m made a 750 m line, longer
	 *  than the engagement range, so the rear never got into the fight. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float LineIntervalCm = 12000.f;

	/** Degrees of helm per metre off the next ahead's wake. 0.25 reaches full
	 *  correction at 120 m off the line, which is one whole interval: further
	 *  out than that she is not keeping station, she is rejoining. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float StationCrossGainDegPerM = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Squadron")
	float StationMaxCorrectionDeg = 30.f;

	/** Past this far off her station she stops keeping it and REJOINS: she
	 *  steers at the station point itself instead of at her next ahead's
	 *  course. Three intervals, so ordinary station keeping never reaches it -
	 *  measured, a squadron with no land in the world stays inside one.
	 *
	 *  The distinction was written into StationCrossGainDegPerM's comment when
	 *  the line of battle was built ("further out than that she is not keeping
	 *  station, she is rejoining") and then never built. It went unnoticed
	 *  because nothing threw a ship far enough off - until land did: measured,
	 *  a follower who touched a bank came off 84 seconds later and finished
	 *  the run 48 km from her station, steering a course parallel to a line
	 *  she was no longer anywhere near. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float RejoinAboveCm = 36000.f;

	/** How far out of station along the line she tolerates before she touches
	 *  the canvas. A square rig has no throttle but her sails. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float StationSlackM = 25.f;

	/** How close a consort may come before this ship bears away from her.
	 *  Two hulls are 31 m long and turn in 73 m; measured without this, three
	 *  ships in company rammed each other six times in five minutes, once at
	 *  an impulse of 8e6. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float ConsortClearanceCm = 12000.f;

	/** How hard she bears away when a consort is close aboard, in degrees of
	 *  heading at the moment of contact. Enough to open the distance, not so
	 *  much that she abandons the engagement. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float ConsortAvoidDeg = 55.f;

	/** Half-width of the lane a shot needs, beyond the angle the consort's own
	 *  hull already subtends: the guns scatter 1.6 degrees in train. */
	UPROPERTY(EditAnywhere, Category = "Squadron")
	float BlockedShotMarginDeg = 2.5f;

	/** --- the land ahead ---------------------------------------------------
	 *
	 *  How far ahead she looks, in SECONDS, so the distance scales with the way
	 *  she has on. Ninety seconds is the real cost of a mistake, measured off
	 *  the recalibrated polar: close-hauled she makes 4.89 m/s and gains 1.04
	 *  m/s to windward, so buying a hundred metres of offing costs about
	 *  ninety-six seconds and nearly five hundred metres of track. A ship who
	 *  sees land ninety seconds ahead can still choose; one who sees it at
	 *  thirty cannot.
	 *
	 *  ZERO BY DEFAULT, which means she does not look ahead at all, and that
	 *  is a decision taken on the measurements rather than a gap.
	 *
	 *  It works: on a coast of three islands it halves her groundings and
	 *  costs her nothing (16 broadsides against 13, the player taking exactly
	 *  the same damage). But on the two geometries where the land lies between
	 *  her and her target it makes her safe and harmless: on a lee shore her
	 *  broadsides fall from 14 to 9 and the player finishes with 760 of his
	 *  hull instead of 400, and against the default island she stops sinking
	 *  him at all. Two geometries out of three lose more fight than they save
	 *  in groundings.
	 *
	 *  And the thing the numbers do not say: an enemy who always weathers the
	 *  point is an enemy the player can never trap against a shore, which is
	 *  the most interesting thing land could add to this game.
	 *
	 *  So it ships off, with -AILookAhead=90 to turn it on. Below about fifty
	 *  seconds it is actively harmful - at twenty she grounded EIGHT times on
	 *  the coast, because she sees the land too late and then saws. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float LandLookAheadSeconds = 0.f;

	/** How much water she wants between her track and the bank. Two ship
	 *  lengths. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float LandClearanceCm = 20000.f;

	/** The scan: candidate courses either side of the one she wants, in steps,
	 *  out to this. Beyond ninety degrees she is not avoiding land any more,
	 *  she is running away from the fight. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float LandScanSpanDeg = 90.f;

	UPROPERTY(EditAnywhere, Category = "Land")
	float LandScanStepDeg = 15.f;

	/** Once she has chosen a course to clear land, she holds it this long.
	 *  Without it the scan re-decides every tick and she saws. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float LandHoldSeconds = 8.f;

	/** --- the ground -----------------------------------------------------
	 *
	 *  What she does about land, and it is exactly one thing: once she is ON
	 *  it, she stops driving herself further on and steers for the sea. There
	 *  is no lookahead and no route. Measured before this existed: driven onto
	 *  a lee shore she lay there 190 seconds of a 400 second run at full sail,
	 *  and with her rig at 0.45 she lay there for the whole of it, her shoal
	 *  reading oscillating between 28 and 38 metres in - pushed off by the
	 *  bank and driving straight back on, because SetSailTrimInput(1.f) ran
	 *  every tick with no knowledge of the ground.
	 *
	 *  Canvas she keeps while working off. A full furl would take her rudder
	 *  with it: the backed-sail authority that lets a square rig steer with no
	 *  way on is gated on trim above 0.1. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float ClawOffTrimFloor = 0.3f;

	/** Closing speed, in m/s, above which she is still driving herself on and
	 *  should be taking canvas in rather than setting it. A sign test with a
	 *  noise floor, not a tuned threshold. */
	UPROPERTY(EditAnywhere, Category = "Land")
	float StillDrivingOnMS = 0.1f;

	/** Cripple her, then sink her. The guns are pointed at the target's rig
	 *  while she still has more than this much of it drawing, and at her hull
	 *  once she does not. A ship that cannot make way cannot escape, and
	 *  cannot choose her range either. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float FireHighAboveRig = 0.5f;

	/** Closing speed, in m/s, below which a ship outside her standoff stops
	 *  trying to lay her guns and runs straight down on her chase.
	 *
	 *  The Engage course trades closing speed for a heading the guns bear
	 *  on, which is right against a ship that stands and fights and wrong
	 *  against one that is making off. Measured against a merchant at half
	 *  canvas, with the raider to windward and 800 m up: she reached 486 m,
	 *  turned to lay her broadside, and sat between 490 and 540 m for two
	 *  hundred seconds without firing a shot, while the merchant walked
	 *  across her bow and into port. A captain who cannot overhaul a laden
	 *  merchant with the weather gauge is not a captain. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float PursuitBelowMS = 1.f;

	/** Inside standoff plus this she gives the chase up and lays the guns
	 *  whatever the closing speed: at that range the turn IS the attack. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float PursuitSlackM = 30.f;

	/** And she does not take the chase up again until the range has opened
	 *  to standoff plus THIS. The hysteresis is on range, not on closing
	 *  speed, because the flapping was on range: a runner opens the range
	 *  the moment the raider turns to bring her guns to bear, so a rule that
	 *  resumed the chase at standoff + 30 resumed it two ticks into every
	 *  turn - 293 "running down" lines in a run, and a raider who zigzagged
	 *  at 350 m instead of finishing either course. 150 m is what a turn
	 *  and a broadside cost against a target making four metres a second. */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	float PursuitResumeM = 150.f;

private:
	/** Nearest heading we can actually sail that is closest to what we want. */
	float ResolveSailableHeading(float DesiredYawDeg, float WindFromBearingDeg,
		float NoGoDeg, float DeltaSeconds);

	/** True when turning the SHORT way from one heading to another sweeps
	 *  through the eye of the wind. A square rig cannot do that without way
	 *  on: it stops dead in the middle and stays there. */
	static bool ArcCrossesWind(float FromYaw, float ToYaw, float WindFromBearingDeg);

	AShipPawn* GetShip() const;
	AShipPawn* FindTarget();

	/** The whole of a merchant's seamanship: sail the laid course, beat if it
	 *  lies in the wind, keep off the land, and lie to once struck or in
	 *  port. No tactics, no guns, no station-keeping - she is not in a line
	 *  of battle and has nothing to fire. */
	void TickMerchant(AShipPawn* Me, float DeltaSeconds);

	/** True when a consort lies between this ship and where she is aiming.
	 *  A squadron that fires through its own line is not a squadron. */
	bool ShotIsBlocked(const AShipPawn* Me, const FVector& FireDir,
		float TargetRange, const AShipPawn* At) const;

	/** Heading correction away from a consort who is too close aboard. */
	float ConsortAvoidance(const AShipPawn* Me) const;

	/** How close her projected TRACK comes to any bank over the look-ahead,
	 *  in centimetres outside it. Negative means she would be in it. A large
	 *  positive number when there is no land at all. */
	float ClearanceOnCourse(const AShipPawn* Me, float CourseYawDeg,
		float SpeedCmS, float ReachCapCm) const;

	/** The course nearest the one she wants that keeps her clear. Returns
	 *  DesiredYaw untouched when nothing is in the way. */
	float CourseClearOfLand(const AShipPawn* Me, float DesiredYawDeg,
		float WindFromBearingDeg, float DeltaSeconds, float ReachCapCm);

	/** The ship this one keeps station on, or null if she leads. */
	AShipPawn* FindNextAhead(const AShipPawn* Me) const;
	void HandleOwnShipSunk(AShipPawn* Ship, AActor* Causer);

	UPROPERTY(Transient)
	TObjectPtr<AShipPawn> Target;

	EShipTactic Tactic = EShipTactic::Close;
	/** Wearing ship: coming round the long way, away from the wind, because
	 *  the short way would have taken her through it. */
	bool bWearing = false;
	float WearSign = 1.f;
	/** +1 / -1 while beating upwind, 0 when the course is directly sailable. */
	int32 TackSign = 0;
	float TackHoldRemaining = 0.f;
	/** Latched so the held-fire line is written once, not sixty times a second. */
	bool bHeldFire = false;

	/** True while she is working herself off the ground. */
	bool bClawingOff = false;
	/** True while she is putting her helm down and going about. Unlike a wear,
	 *  this does NOT take the helm: the ordinary heading error steers her, so
	 *  she turns the short way, which is the whole point. */
	bool bComingAbout = false;
	int32 Wears = 0;
	/** Counted, not assumed: must read 0 in a world with no island. */
	int32 AvoidTicks = 0;
	int32 Avoidances = 0;
	float LandHoldRemaining = 0.f;
	float HeldLandYaw = 0.f;
	bool bAvoiding = false;
	int32 TacksThrough = 0;
	int32 PursuitTicks = 0;
	/** Latched so "running down" and "guns laid again" are each said once
	 *  per spell, not sixty times a second. */
	bool bPursuing = false;
	bool bClawLogged = false;
	/** Counted, not assumed: both must read 0 in a world with no island. */
	int32 LandTicks = 0;
	int32 ClawOffs = 0;
	int32 RejoinTicks = 0;
	bool bRejoining = false;
	/** Latched so "took station" and "has the lead" are each said once. */
	bool bReportedStation = false;
	float RetargetTimer = 0.f;
	float LogTimer = 0.f;

	FVector Destination = FVector::ZeroVector;
	bool bHasDestination = false;
};
