// ngtest -- an N-Gage application, built with the N-Gage SDK's own compiler
// (GCC 2.9-psion-98r2, Series 60 6.1 headers), that uses the audio and thread
// APIs the way One does and writes what it measured to C:\ngtest.txt.
//
// It runs through the gate6 loader like any title (games/ngtest), so on the
// bench it tests the port's translation of those calls, and on a phone it
// measures what the platform itself does: how long a Stop takes, whether a
// worker's Stop waits for a busy main thread, and what playing streams cost
// a thread that wants the CPU (rounds 131-133: the freeze and the half-rate
// fights).
//
// Phases, each logged as it ends:
//   A  the main thread's stream: open (package filled), set up, play two
//      seconds keeping one buffer in flight as One's writer does, Position
//      against written, Stop (how long, and whether PlayComplete came inside
//      it), WriteL after the Stop, then starve it and time the underflow.
//   B  a second of busy loop on the main thread with nothing playing.
//   C  a worker thread plays its own stream; a second of busy loop (one
//      stream playing); then the worker stops its stream while the main
//      thread is in a two-second busy loop, and records how long its Stop took.
//   D  two workers playing; a second of busy loop (two streams).
//   E  a -101 frame loop with the main stream fed from its callbacks, then
//      with a worker's stream too; the longest gap between copies.
//   F  a spinner thread at the main thread's priority against a busy second,
//      sampled every 100 ms: does one priority share the CPU.
//   C2/C3  a worker's Stop with the main thread idle, then with it busy for
//      five seconds across the Stop; twice each.
//   G  One's writer as it is: an Open package with no rate or channels, then
//      SetAudioPropertiesL; 1280-sample buffers, written when fewer than 1120
//      are ahead by Position, one in flight. G1 as is, G2 with 1600 more
//      ahead (the port's 100 ms Position lead), G3 as G1 with the frame loop.
//   H  a stream opened and left with nothing for half a second, then one
//      buffer: what it does starved, and how long the first copy takes.
// Phase A runs three times, for the scatter. The screen is purple while it
// runs, and the application closes itself at DONE.
// No writable static data: an EKA1 .app has none, so everything hangs off
// heap objects.

#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <coecntrl.h>
#include <f32file.h>
#include <mdaaudiooutputstream.h>
#include <mda/common/audio.h>

const TUid KUidNgTest = { 0x10205E7A };

enum { KRate = 16000, KBufSamples = 1600, KBufBytes = KBufSamples * 2 };

// ---------------------------------------------------------------------------
// The log: lines of text to C:\ngtest.txt, written through at once so a hang
// leaves everything before it.
class CLog : public CBase
    {
public:
    static CLog* NewL()
        {
        CLog* self = new (ELeave) CLog;
        CleanupStack::PushL(self);
        User::LeaveIfError(self->iFs.Connect());
        User::LeaveIfError(self->iFile.Replace(self->iFs, _L("C:\\ngtest.txt"), EFileWrite | EFileShareAny));
        CleanupStack::Pop(self);
        return self;
        }
    ~CLog() { iFile.Close(); iFs.Close(); }
    void Line(TRefByValue<const TDesC> aFmt, ...)
        {
        VA_LIST list;
        VA_START(list, aFmt);
        TBuf<160> b;
        b.FormatList(aFmt, list);
        TBuf8<200> b8;
        b8.Copy(b);
        b8.Append('\n');
        iFile.Write(b8);
        iFile.Flush();
        }
private:
    RFs iFs;
    RFile iFile;
    };

static void FillTone(TDes8& aBuf, TInt aPhase, TInt aSamples = KBufSamples)
    {
    // A quiet square wave at 400 Hz: no floating point, no tables.
    aBuf.SetLength(aSamples * 2);
    TUint16* p = (TUint16*)aBuf.Ptr();
    for (TInt i = 0; i < aSamples; i++)
        p[i] = (TUint16)((((aPhase + i) / 20) & 1) ? 1500 : (TUint16)-1500);
    }

static TInt NowMs() { TTime t; t.UniversalTime(); return (t.Int64() / TInt64(1000)).Low(); }

static TInt64 PositionUs(CMdaAudioOutputStream* aStream)
    {
    return aStream->Position().Int64();
    }

static TInt PositionSamples(CMdaAudioOutputStream* aStream)
    {
    return (TInt)(PositionUs(aStream) / TInt64(1000)).Low() * (KRate / 1000);
    }

// The gaps between one stream's buffer copies: the phone's own measure of a
// dropout, since it has no capture to read.
struct TGaps
    {
    TInt iLast, iMax, iOver150, iN;
    void Reset() { iLast = 0; iMax = 0; iOver150 = 0; iN = 0; }
    void Copy(TInt aNow)
        {
        if (iLast)
            {
            TInt g = aNow - iLast;
            if (g > iMax) iMax = g;
            if (g > 150) iOver150++;
            }
        iLast = aNow;
        iN++;
        }
    };

// ---------------------------------------------------------------------------
// A worker: its own scheduler, its own stream, a fixed script, and its
// timings written into a shared record the main thread reads afterwards.
struct TWorkerShared
    {
    TInt iPlayMs;           // how long to play before stopping
    TInt iOpenErr;
    TInt iBuffers;          // buffers copied
    TUint32 iStopStartTick; // FastCounter round the worker's Stop
    TUint32 iStopEndTick;
    TInt iStopStartMs;      // real time, ms, the same
    TInt iStopEndMs;
    TInt iPlayCompleteErr;
    TInt iPlayCompleteInStop;
    TInt iDone;
    TGaps iGaps;
    };

class CWorkerPlayer : public CActive, public MMdaAudioOutputStreamCallback
    {
public:
    CWorkerPlayer(TWorkerShared* aShared) : CActive(EPriorityStandard), iShared(aShared), iBufPtr(0, 0)
        {
        CActiveScheduler::Add(this);
        }
    ~CWorkerPlayer()
        {
        Cancel();        iShared->iDone = 20;
        iTimer.Close();  iShared->iDone = 21;
        delete iStream;  iShared->iDone = 22;
        delete iBuf;     iShared->iDone = 23;
        }
    void StartL()
        {
        User::LeaveIfError(iTimer.CreateLocal());
        iBuf = HBufC8::NewL(KBufBytes);
        iBufPtr.Set(iBuf->Des());
        iStream = CMdaAudioOutputStream::NewL(*this);
        iSettings.iSampleRate = TMdaAudioDataSettings::ESampleRate16000Hz;
        iSettings.iChannels = TMdaAudioDataSettings::EChannelsMono;
        iSettings.iFlags = TMdaAudioDataSettings::ENoNetworkRouting;
        iSettings.iVolume = 1;
        iStream->Open(&iSettings);
        }
    void MaoscOpenComplete(TInt aError)
        {
        iShared->iOpenErr = aError;
        if (aError != KErrNone) { Finish(); return; }
        TRAPD(err, iStream->SetAudioPropertiesL(TMdaAudioDataSettings::ESampleRate16000Hz, TMdaAudioDataSettings::EChannelsMono));
        iStream->SetVolume(iStream->MaxVolume() / 4);
        Write();
        iTimer.After(iStatus, iShared->iPlayMs * 1000);
        SetActive();
        }
    void MaoscBufferCopied(TInt aError, const TDesC8&)
        {
        if (aError == KErrNone && !iStopped) { iShared->iBuffers++; iShared->iGaps.Copy(NowMs()); Write(); }
        }
    void MaoscPlayComplete(TInt aError)
        {
        iShared->iPlayCompleteErr = aError;
        iShared->iPlayCompleteInStop = iInStop;
        if (!iInStop) Finish();
        }
    void RunL()
        {
        // Time to stop: the call whose length is the question.
        iStopped = ETrue;
        iInStop = ETrue;
        iShared->iStopStartTick = User::FastCounter();
        iShared->iStopStartMs = NowMs();
        iStream->Stop();
        iShared->iStopEndTick = User::FastCounter();
        iShared->iStopEndMs = NowMs();
        iInStop = EFalse;
        Finish();
        }
    void DoCancel() { iTimer.Cancel(); }
private:
    void Write()
        {
        FillTone(iBufPtr, iPhase);
        iPhase += KBufSamples;
        TRAPD(err, iStream->WriteL(iBufPtr));
        }
    void Finish()
        {
        if (iFinished) return;
        iFinished = ETrue;
        iShared->iDone = 1;
        CActiveScheduler::Stop();
        }
    TWorkerShared* iShared;
    CMdaAudioOutputStream* iStream;
    TMdaAudioDataSettings iSettings;
    HBufC8* iBuf;
    TPtr8 iBufPtr;
    RTimer iTimer;
    TInt iPhase;
    TBool iStopped;
    TBool iInStop;
    TBool iFinished;
    };

static TInt WorkerMain(TAny* aShared)
    {
    CTrapCleanup* cleanup = CTrapCleanup::New();
    CActiveScheduler* sched = new CActiveScheduler;
    CActiveScheduler::Install(sched);
    CWorkerPlayer* player = new CWorkerPlayer((TWorkerShared*)aShared);
    TRAPD(err, player->StartL());
    if (err == KErrNone)
        CActiveScheduler::Start();
    else
        ((TWorkerShared*)aShared)->iOpenErr = err;
    TWorkerShared* sh = (TWorkerShared*)aShared;
    sh->iDone = 10;             // stages of the teardown, read back by the main thread
    delete player;
    sh->iDone = 11;
    // The scheduler is not deleted. `delete` from GCC98r2 code calls vtable
    // word 2 with in-charge 3, and on the 9.x object that word is Extension_
    // (BUGBOOK, "Deleting a 9.x object through the old vtable word"): a
    // KERN-EXEC 3 here at every teardown (round 133). The port's
    // old_deletable covers CDir and CFileMan, not this; no title deletes its
    // scheduler, and the thread's exit frees its heap anyway.
    (void)sched;
    sh->iDone = 12;
    delete cleanup;
    sh->iDone = 2;
    return 0;
    }

// ---------------------------------------------------------------------------
// Phase F: a spinner at the main thread's own priority, no audio, no calls --
// whether two busy threads of one priority share the CPU (an EKA2 kernel
// round-robins them on its timeslice; on the bench, round 133's worker
// streams went silent for 800 ms while the main thread was busy).
struct TSpinShared { volatile TInt iStop; volatile TInt iCount; volatile TInt iSeenStop; TAny* volatile iArg; volatile TInt iEntryMs; };
static TInt SpinMain(TAny* aShared)
    {
    TSpinShared* s = (TSpinShared*)aShared;
    s->iArg = aShared;
    s->iEntryMs = NowMs();
    s->iSeenStop = s->iStop;
    s->iCount = 1;
    while (!s->iStop)
        for (TInt i = 0; i < 1024; i++) s->iCount++;
    return 0;
    }

// ---------------------------------------------------------------------------
// A frame loop as One's: an active object that completes itself, at the
// priority the port gives One's (-101, below everything), with a fixed piece
// of work a "frame" (phase E).
class CKick : public CActive
    {
public:
    CKick() : CActive(-101) { CActiveScheduler::Add(this); }
    ~CKick() { Cancel(); }
    void Go() { iRuns = 0; iOn = ETrue; Kick(); }
    void Halt() { iOn = EFalse; }
    TInt iRuns;
private:
    void Kick() { TRequestStatus* s = &iStatus; iStatus = KRequestPending; SetActive(); User::RequestComplete(s, KErrNone); }
    void RunL()
        {
        volatile TInt x = 1;
        for (TInt i = 0; i < 20000; i++) x = x * 3 + 1;
        iRuns++;
        if (iOn) Kick();
        }
    void DoCancel() {}
    TBool iOn;
    };

// ---------------------------------------------------------------------------
// The application's one control: purple while the phases run. At DONE the
// application closes itself (User::Exit, as the Asphalts quit, BUGBOOK 1.u):
// a later redraw did not reach the screen through the port on the bench
// (E696-E698: DrawNow, then DrawDeferred, never redrawn), and an app that
// closes is a signal nobody can miss. Purple has red equal to blue: an
// inline TRgb from the 6.1 headers packs its channels in the old order, and
// 9.x reads red and blue swapped.
class CBlank : public CCoeControl
    {
public:
    void ConstructL(const TRect& aRect) { iColour = TRgb(128, 0, 128); CreateWindowL(); SetRect(aRect); ActivateL(); }
    void Draw(const TRect&) const
        {
        CWindowGc& gc = SystemGc();
        gc.SetBrushColor(iColour);
        gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.Clear(Rect());
        }
private:
    TRgb iColour;
    };

// ---------------------------------------------------------------------------
// One stream on the main thread with a script of its own (phases G and H).
// The runner starts it, gives it a fixed time and reads its counts back.
enum { KOneSamples = 1280, KOneRefill = 1120 };   // One's writer, E662 and the round-132 dumps

class CStreamProbe : public CActive, public MMdaAudioOutputStreamCallback
    {
public:
    enum TMode { EOneWriter, EStarved };
    CStreamProbe(CLog* aLog, TMode aMode, TInt aThreshold, TBool aEmptyPackage)
        : CActive(EPriorityStandard), iLog(aLog), iMode(aMode), iThreshold(aThreshold),
          iEmptyPackage(aEmptyPackage), iBufPtr(0, 0)
        {
        CActiveScheduler::Add(this);
        }
    ~CStreamProbe()
        {
        Cancel();
        iTimer.Close();
        delete iStream;
        delete iBuf;
        }
    void StartL()
        {
        User::LeaveIfError(iTimer.CreateLocal());
        iBuf = HBufC8::NewL(KBufBytes);
        iBufPtr.Set(iBuf->Des());
        iGaps.Reset();
        iAheadMin = 0x7fffffff;
        iAheadMax = -0x7fffffff;
        iStream = CMdaAudioOutputStream::NewL(*this);
        if (iEmptyPackage)
            {
            // As One hands it over (E664): rate and channels 0, the default.
            iSettings.iSampleRate = 0;
            iSettings.iChannels = 0;
            }
        else
            {
            iSettings.iSampleRate = TMdaAudioDataSettings::ESampleRate16000Hz;
            iSettings.iChannels = TMdaAudioDataSettings::EChannelsMono;
            }
        iSettings.iFlags = TMdaAudioDataSettings::ENoNetworkRouting;
        iSettings.iVolume = 1;
        iT0 = NowMs();
        iStream->Open(&iSettings);
        }
    // Ends the script: a Stop whose callbacks are not counted.
    void Halt()
        {
        if (iHalted) return;
        iHalted = ETrue;
        Cancel();
        if (iStream && iOpened)
            {
            TInt t0 = NowMs();
            iStream->Stop();
            iStopMs = NowMs() - t0;
            }
        }

    void MaoscOpenComplete(TInt aError)
        {
        iOpenErr = aError;
        iOpenMs = NowMs() - iT0;
        if (aError != KErrNone || iHalted) return;
        iOpened = ETrue;
        TRAP(iPropsErr, iStream->SetAudioPropertiesL(TMdaAudioDataSettings::ESampleRate16000Hz, TMdaAudioDataSettings::EChannelsMono));
        iStream->SetVolume(iStream->MaxVolume() / 4);
        iStart = NowMs();
        iTimer.After(iStatus, iMode == EStarved ? 500000 : 20000);
        SetActive();
        }
    void MaoscBufferCopied(TInt aError, const TDesC8&)
        {
        if (iHalted) return;
        iInFlight = EFalse;
        if (aError != KErrNone) { iCopyErrs++; iCopyErr = aError; return; }
        TInt now = NowMs();
        iGaps.Copy(now);
        if (!iFirstCopyMs)
            {
            iFirstCopyMs = now - iFirstWrite;
            iFirstCopyPos = PositionSamples(iStream);
            }
        }
    void MaoscPlayComplete(TInt aError)
        {
        if (iHalted) return;
        iInFlight = EFalse;
        iPlayCompletes++;
        iPlayCompleteErr = aError;
        if (aError == KErrUnderflow) iUnderflows++;
        if (!iPlayCompleteMs) iPlayCompleteMs = NowMs() - (iFirstWrite ? iFirstWrite : iStart);
        }
    void Report(const TDesC& aTag)
        {
        iLog->Line(_L("%S open err=%d %d ms, props err=%d, stop %d ms"), &aTag, iOpenErr, iOpenMs, iPropsErr, iStopMs);
        if (iMode == EOneWriter)
            {
            TInt avg = iAheadN ? iAheadSum / iAheadN : 0;
            iLog->Line(_L("%S writes %d copies %d (errors %d, last %d), underflows %d of %d play-completes"),
                &aTag, iWrites, iGaps.iN, iCopyErrs, iCopyErr, iUnderflows, iPlayCompletes);
            iLog->Line(_L("%S ahead by Position, samples: min %d avg %d max %d over %d polls; copy gap max %d ms, %d over 150 ms"),
                &aTag, iAheadMin, avg, iAheadMax, iAheadN, iGaps.iMax, iGaps.iOver150);
            }
        else
            iLog->Line(_L("%S starved 500 ms: %d play-completes before the write; first copy %d ms after it at Position %d; play-complete err=%d at %d ms"),
                &aTag, iEarlyCompletes, iFirstCopyMs, iFirstCopyPos, iPlayCompleteErr, iPlayCompleteMs);
        }

private:
    void RunL()
        {
        if (iHalted) return;
        if (iMode == EStarved)
            {
            if (!iFirstWrite)
                {
                iEarlyCompletes = iPlayCompletes;
                iPlayCompletes = 0;
                iPlayCompleteMs = 0;
                iFirstWrite = NowMs();
                Write(KBufSamples);
                }
            return;
            }
        // One's writer: by Position, top up one buffer when it runs low.
        TInt ahead = iWritten - PositionSamples(iStream);
        if (ahead < iAheadMin) iAheadMin = ahead;
        if (ahead > iAheadMax) iAheadMax = ahead;
        iAheadSum += ahead;
        iAheadN++;
        if (!iInFlight && ahead < iThreshold)
            Write(KOneSamples);
        iTimer.After(iStatus, 20000);
        SetActive();
        }
    void DoCancel() { iTimer.Cancel(); }
    void Write(TInt aSamples)
        {
        FillTone(iBufPtr, iWritten, aSamples);
        iWritten += aSamples;
        iWrites++;
        iInFlight = ETrue;
        if (!iFirstWrite) iFirstWrite = NowMs();
        TRAPD(err, iStream->WriteL(iBufPtr));
        if (err) { iInFlight = EFalse; iCopyErrs++; iCopyErr = err; }
        }

    CLog* iLog;
    TMode iMode;
    TInt iThreshold;
    TBool iEmptyPackage;
    CMdaAudioOutputStream* iStream;
    TMdaAudioDataSettings iSettings;
    HBufC8* iBuf;
    TPtr8 iBufPtr;
    RTimer iTimer;
    TBool iOpened, iHalted, iInFlight;
    TInt iT0, iStart, iOpenErr, iOpenMs, iPropsErr, iStopMs;
    TInt iWritten, iWrites, iCopyErrs, iCopyErr, iUnderflows, iPlayCompletes, iPlayCompleteErr, iPlayCompleteMs;
    TInt iAheadMin, iAheadMax, iAheadSum, iAheadN;
    TInt iFirstWrite, iFirstCopyMs, iFirstCopyPos, iEarlyCompletes;
    TGaps iGaps;
    };

// ---------------------------------------------------------------------------
// The main thread's runner: one active object, a state machine of phases.
class CRunner : public CActive, public MMdaAudioOutputStreamCallback
    {
public:
    enum TState { EIdle, EAOpen, EAPlay, EAStop, EAStarve, EBDone, ECWarm, ECOneBusy, ECWaitStop, ECStopBusy,
                  ECJoin, EDWarm, EDBusy, EDJoin, EEOpen, EEKick, EEKick2, EEEnd, EDone,
                  EARepeat, EC2Log, EC3Busy, EC3Log, EGEnd, EHEnd };
    static CRunner* NewL(CBlank* aBlank)
        {
        CRunner* self = new (ELeave) CRunner;
        self->iBlank = aBlank;
        CleanupStack::PushL(self);
        self->ConstructL();
        CleanupStack::Pop(self);
        return self;
        }
    ~CRunner()
        {
        Cancel();
        iTimer.Close();
        delete iProbe;
        delete iKick;
        delete iStream;
        delete iBuf;
        delete iLog;
        for (TInt i = 0; i < 2; i++) { delete iShared[i]; iThread[i].Close(); }
        }

    // MMdaAudioOutputStreamCallback, for the main thread's stream (phase A).
    void MaoscOpenComplete(TInt aError)
        {
        Cancel();               // the watchdog
        iLog->Line(_L("A%d open-complete err=%d after %d ms"), iARound + 1, aError, Ms() - iT0);
        if (aError != KErrNone) { Next(EBDone, 10); return; }
        TRAPD(err, iStream->SetAudioPropertiesL(TMdaAudioDataSettings::ESampleRate16000Hz, TMdaAudioDataSettings::EChannelsMono));
        TInt max = iStream->MaxVolume();
        iStream->SetVolume(max / 4);
        iLog->Line(_L("A props err=%d maxvol=%d vol=%d"), err, max, iStream->Volume());
        if (iPhaseE)
            {
            iState = EEKick;
            Write();
            if (!iKick) iKick = new CKick;
            iKickStart = Ms();
            iKickCopies0 = iCopies;
            iGaps.Reset();
            iKick->Go();
            Next(EEKick, 3000);
            return;
            }
        iState = EAPlay;
        iPlayStart = Ms();
        Write();
        }
    void MaoscBufferCopied(TInt aError, const TDesC8&)
        {
        iCopies++;
        if (aError != KErrNone) { iCopyErr = aError; iCopyErrs++; return; }
        iGaps.Copy(Ms());
        TInt64 pos = PositionUs(iStream);
        TInt played = (TInt)(pos / TInt64(1000)).Low() * (KRate / 1000);
        if ((iCopies % 4) == 1 && !iPhaseE)
            iLog->Line(_L("A%d copied #%d at %d ms (fast %u): written %d played %d ahead %d"),
                iARound + 1, iCopies, Ms() - iPlayStart, User::FastCounter(), iWritten, played, iWritten - played);
        if (iPhaseE)
            {
            Write();
            return;
            }
        if (iState == EAPlay)
            {
            if (Ms() - iPlayStart < 2000)
                Write();
            else
                Next(EAStop, 0);    // from the runner, not inside this callback (as One stops from its frame)
            }
        else if (iState == EAStarve && iAfterStopWrites < 3)
            {
            iAfterStopWrites++;
            Write();
            }
        }
    void MaoscPlayComplete(TInt aError)
        {
        iLog->Line(_L("A%d play-complete err=%d inside-stop=%d at %d ms"), iARound + 1, aError, iInStop, Ms() - iPlayStart);
        if (!iInStop && iState == EAStarve)
            {
            iLog->Line(_L("A%d underflow came %d ms after the last write"), iARound + 1, Ms() - iLastWrite);
            Next(iARound < 2 ? EARepeat : EBDone, 200);
            }
        }

private:
    CRunner() : CActive(EPriorityStandard), iBufPtr(0, 0) { CActiveScheduler::Add(this); }
    void ConstructL()
        {
        iLog = CLog::NewL();
        User::LeaveIfError(iTimer.CreateLocal());
        iBuf = HBufC8::NewL(KBufBytes);
        iBufPtr.Set(iBuf->Des());
        iLog->Line(_L("ngtest 3: FastCounter %u, TickCount %u"), User::FastCounter(), User::TickCount());
        iT0 = Ms();
        iStartMs = iT0;
        iGaps.Reset();
        Next(EAOpen, 500);
        }
    // Real time in ms, from the clock rather than TickCount, whose rate is
    // the kernel's tick and differs between the phone and the bench.
    TInt Ms() { return NowMs(); }
    static TInt Pct(TInt a, TInt b) { return (b / 100) ? a / (b / 100) : 0; }
    void Next(TState aState, TInt aMs)
        {
        Cancel();               // the open watchdog may still be pending
        iState = aState;
        iTimer.After(iStatus, aMs * 1000);
        SetActive();
        }
    void Write()
        {
        FillTone(iBufPtr, iWritten);
        iWritten += KBufSamples;
        iLastWrite = Ms();
        TRAPD(err, iStream->WriteL(iBufPtr));
        if (err) iLog->Line(_L("A WriteL left %d"), err);
        }
    void StopMainStream()
        {
        TUint32 f0 = User::FastCounter();
        TInt t0 = Ms();
        iInStop = ETrue;
        iStream->Stop();
        iInStop = EFalse;
        iLog->Line(_L("A%d Stop took %d ms (%u fast ticks); copies %d, errors %d (last %d); copy gap max %d ms, %d over 150 ms"),
            iARound + 1, Ms() - t0, User::FastCounter() - f0, iCopies, iCopyErrs, iCopyErr, iGaps.iMax, iGaps.iOver150);
        // WriteL after a Stop, without Open: does the stream start again?
        iAfterStopWrites = 0;
        Next(EAStarve, 5000);   // a watchdog: the underflow moves the state on
        Write();
        }
    // A second of busy loop: iterations, and the FastCounter it spanned.
    TInt Busy(TInt aMs)
        {
        const TInt end = Ms() + aMs;
        TInt n = 0;
        volatile TInt x = 1;
        while (Ms() < end)
            for (TInt i = 0; i < 1024; i++) { x = x * 3 + 1; n++; }
        return n;
        }
    TInt StartWorker(TInt aIndex, TInt aPlayMs)
        {
        iShared[aIndex] = new TWorkerShared;
        Mem::FillZ(iShared[aIndex], sizeof(TWorkerShared));
        iShared[aIndex]->iPlayMs = aPlayMs;
        TBuf<16> name;
        name.Format(_L("ngw%d"), aIndex);
        TInt err = iThread[aIndex].Create(name, WorkerMain, 0x4000, 0x1000, 0x100000, iShared[aIndex]);
        if (err == KErrNone)
            iThread[aIndex].Resume();
        iLog->Line(_L("worker %d created err=%d"), aIndex, err);
        return err;
        }
    void LogWorker(TInt aIndex)
        {
        TWorkerShared* s = iShared[aIndex];
        if (!s) { iLog->Line(_L("worker %d: none"), aIndex); return; }
        iLog->Line(_L("worker %d: open err=%d buffers=%d stop %d ms (%u fast ticks), play-complete err=%d inside-stop=%d done=%d"),
            aIndex, s->iOpenErr, s->iBuffers, s->iStopEndMs - s->iStopStartMs,
            s->iStopEndTick - s->iStopStartTick, s->iPlayCompleteErr, s->iPlayCompleteInStop, s->iDone);
        iLog->Line(_L("worker %d: copy gap max %d ms, %d over 150 ms"), aIndex, s->iGaps.iMax, s->iGaps.iOver150);
        }
    // A finished worker's thread and record, freed for the next phase. One
    // still running keeps its record (leaked rather than freed under it).
    void ReleaseWorker(TInt aIndex)
        {
        if (!iShared[aIndex]) return;
        if (iThread[aIndex].ExitType() == EExitPending)
            iLog->Line(_L("worker %d still running at release"), aIndex);
        else
            delete iShared[aIndex];
        iShared[aIndex] = NULL;
        iThread[aIndex].Close();
        }

    void StartC2()
        {
        StartWorker(0, 3000);
        Next(EC2Log, 4500);
        }
    void StartG()
        {
        // G1 One's threshold, G2 1600 samples more (the port's lead), G3 G1
        // with the frame loop running.
        TInt threshold = (iRep == 1) ? KOneRefill + 1600 : KOneRefill;
        iProbe = new (ELeave) CStreamProbe(iLog, CStreamProbe::EOneWriter, threshold, ETrue);
        TRAPD(err, iProbe->StartL());
        if (err) iLog->Line(_L("G%d start left %d"), iRep + 1, err);
        if (iRep == 2)
            {
            if (!iKick) iKick = new CKick;
            iKick->Go();
            }
        Next(EGEnd, 6000);
        }
    void StartH()
        {
        iProbe = new (ELeave) CStreamProbe(iLog, CStreamProbe::EStarved, 0, EFalse);
        TRAPD(err, iProbe->StartL());
        if (err) iLog->Line(_L("H%d start left %d"), iRep + 1, err);
        Next(EHEnd, 2500);
        }
    // Phase F: a spinner at the main thread's priority against a busy
    // second, its count read every 100 ms.
    void RunSpinner(TInt aRep)
        {
        TSpinShared* sp = new TSpinShared;
        sp->iStop = 0;
        sp->iCount = 0;
        sp->iSeenStop = -1;
        sp->iArg = 0;
        RThread spin;
        TBuf<16> name;
        name.Format(_L("ngspin%d"), aRep);
        TInt err = spin.Create(name, SpinMain, 0x2000, 0x1000, 0x10000, sp);
        if (err != KErrNone)
            {
            iLog->Line(_L("F%d spinner not created: %d"), aRep, err);
            delete sp;
            return;
            }
        TRequestStatus st;
        spin.Logon(st);
        if (aRep == 1)
            iLog->Line(_L("F priorities: main %d, spinner %d"), (TInt)RThread().Priority(), (TInt)spin.Priority());
        spin.Resume();
        User::After(100000);        // let it through its startup and into the loop
        TInt c0 = sp->iCount;
        TInt d[10];
        TInt n = 0;
        TInt prev = c0;
        for (TInt i = 0; i < 10; i++)
            {
            n += Busy(100);
            TInt c = sp->iCount;
            d[i] = c - prev;
            prev = c;
            }
        sp->iStop = 1;
        User::WaitForRequest(st);
        iLog->Line(_L("F%d one second, main busy and a same-priority spinner: main %d iterations (%d%% of B), spinner +%d (%d in its first 100 ms alone)"),
            aRep, n, Pct(n, iBusy0), prev - c0, c0);
        iLog->Line(_L("F%d spinner per 100 ms: %d %d %d %d %d %d %d %d %d %d"),
            aRep, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9]);
        if (sp->iSeenStop != 0)
            iLog->Line(_L("F%d spinner saw stop at entry (%d): it never ran before the busy second"), aRep, sp->iSeenStop);
        spin.Close();
        delete sp;
        }

    void RunL()
        {
        switch (iState)
            {
        case EAOpen:
            {
            iStream = CMdaAudioOutputStream::NewL(*this);
            iSettings.iSampleRate = TMdaAudioDataSettings::ESampleRate16000Hz;
            iSettings.iChannels = TMdaAudioDataSettings::EChannelsMono;
            iSettings.iFlags = TMdaAudioDataSettings::ENoNetworkRouting;
            iSettings.iVolume = 1;
            iLog->Line(_L("A open"));
            iT0 = Ms();
            iStream->Open(&iSettings);
            Next(EAOpen, 8000);   // a watchdog: open-complete moves the state on
            iState = EIdle;
            break;
            }
        case EIdle:
            iLog->Line(_L("A no open-complete in 8 s; going on"));
            Next(EBDone, 10);
            break;
        case EAStop:
            StopMainStream();
            break;
        case EAStarve:
            // The N95 (round 134): a stream restarted by WriteL after its Stop
            // and then starved said nothing in five seconds. That is a
            // result, and the repeats still run.
            iLog->Line(_L("A%d no play-complete in 5 s after starving (last write %d ms ago)"), iARound + 1, Ms() - iLastWrite);
            Next(iARound < 2 ? EARepeat : EBDone, 10);
            break;
        case EBDone:
            {
            delete iStream;
            iStream = NULL;
            TUint32 f0 = User::FastCounter();
            iBusy0 = Busy(1000);
            iLog->Line(_L("B busy 1 s, nothing playing: %d iterations (%u fast ticks)"), iBusy0, User::FastCounter() - f0);
            StartWorker(0, 4000);
            Next(ECWarm, 1000);
            break;
            }
        case ECWarm:
            {
            TInt n = Busy(1000);
            iLog->Line(_L("C busy 1 s, one stream playing: %d iterations (%d%% of B)"), n, Pct(n, iBusy0));
            Next(ECWaitStop, 1200);      // now ~2.2 s into the worker's 4 s
            break;
            }
        case ECWaitStop:
            {
            // Busy from 3.2 s to 5.2 s: the worker's Stop falls at 4 s, inside.
            TInt n = Busy(2000);
            iLog->Line(_L("C busy 2 s across the worker's Stop: %d iterations"), n);
            Next(ECJoin, 1500);
            break;
            }
        case ECJoin:
            LogWorker(0);
            ReleaseWorker(0);
            StartWorker(1, 6000);
            StartWorker(0, 6000);
            Next(EDWarm, 1500);
            break;
        case EDWarm:
            {
            TInt n = Busy(1000);
            iLog->Line(_L("D busy 1 s, two streams playing: %d iterations (%d%% of B)"), n, Pct(n, iBusy0));
            Next(EDJoin, 6000);
            break;
            }
        case EDJoin:
            LogWorker(0);
            LogWorker(1);
            // Phase E: the fight's shape -- the main thread's stream fed from
            // its callbacks while a -101 frame loop runs; then the same with
            // a worker's stream playing too.
            iPhaseE = ETrue;
            iCopies = 0;
            iWritten = 0;
            iStream = CMdaAudioOutputStream::NewL(*this);
            iStream->Open(&iSettings);
            Next(EEOpen, 8000);
            break;
        case EEOpen:
            iLog->Line(_L("E no open-complete; going on"));
            Next(EEEnd, 10);
            break;
        case EEKick:
            {
            iKick->Halt();
            TInt t = Ms() - iKickStart;
            iLog->Line(_L("E frame loop, main stream playing: %d frames in %d ms, %d buffers copied, copy gap max %d ms, %d over 150 ms"),
                iKick->iRuns, t, iCopies - iKickCopies0, iGaps.iMax, iGaps.iOver150);
            ReleaseWorker(0);
            ReleaseWorker(1);
            StartWorker(0, 5000);
            Next(EEKick2, 1500);
            iState = EEKick2;
            iKickPhase = 1;
            break;
            }
        case EEKick2:
            if (iKickPhase == 1)
                {
                iKickPhase = 2;
                iKickStart = Ms();
                iKickCopies0 = iCopies;
                iGaps.Reset();
                iKick->Go();
                Next(EEKick2, 3000);
                }
            else
                {
                iKick->Halt();
                TInt t = Ms() - iKickStart;
                iLog->Line(_L("E frame loop, main stream and a worker's playing: %d frames in %d ms, %d buffers copied, copy gap max %d ms, %d over 150 ms"),
                    iKick->iRuns, t, iCopies - iKickCopies0, iGaps.iMax, iGaps.iOver150);
                Next(EEEnd, 2000);
                }
            break;
        case EEEnd:
            if (iStream) iStream->Stop();
            LogWorker(0);
            ReleaseWorker(0);
            RunSpinner(1);
            RunSpinner(2);
            iRep = 0;
            StartC2();
            break;
        case EARepeat:
            delete iStream;
            iStream = NULL;
            iARound++;
            iCopies = 0;
            iWritten = 0;
            iCopyErrs = 0;
            iCopyErr = 0;
            iGaps.Reset();
            Next(EAOpen, 300);
            break;
        case EC2Log:
            // C2: the worker stopped at 3 s with the main thread idle -- the
            // baseline for C3's Stop under a busy main thread.
            iLog->Line(_L("C2.%d worker's Stop with the main thread idle:"), iRep + 1);
            LogWorker(0);
            ReleaseWorker(0);
            StartWorker(0, 3000);
            Next(EC3Busy, 1000);
            break;
        case EC3Busy:
            {
            // C3: busy from 1 s to 6 s; the worker's Stop falls at 3 s.
            TInt n = Busy(5000);
            iLog->Line(_L("C3.%d busy 5 s across the worker's Stop: %d iterations (%d%% of B per second)"),
                iRep + 1, n, Pct(n / 5, iBusy0));
            Next(EC3Log, 1500);
            break;
            }
        case EC3Log:
            iLog->Line(_L("C3.%d worker's Stop with the main thread busy:"), iRep + 1);
            LogWorker(0);
            ReleaseWorker(0);
            if (++iRep < 2)
                StartC2();
            else
                {
                iRep = 0;
                StartG();
                }
            break;
        case EGEnd:
            {
            if (iRep == 2 && iKick) iKick->Halt();
            iProbe->Halt();
            TBuf<4> tag;
            tag.Format(_L("G%d"), iRep + 1);
            iProbe->Report(tag);
            if (iRep == 2 && iKick)
                iLog->Line(_L("G3 frame loop: %d frames in the run"), iKick->iRuns);
            delete iProbe;
            iProbe = NULL;
            if (++iRep < 3)
                StartG();
            else
                {
                iRep = 0;
                StartH();
                }
            break;
            }
        case EHEnd:
            {
            iProbe->Halt();
            TBuf<4> tag;
            tag.Format(_L("H%d"), iRep + 1);
            iProbe->Report(tag);
            delete iProbe;
            iProbe = NULL;
            if (++iRep < 2)
                StartH();
            else
                {
                iLog->Line(_L("DONE in %d s"), (Ms() - iStartMs) / 1000);
                iState = EDone;
                delete iLog;            // closed, so the file is whole before the process goes
                iLog = NULL;
                User::Exit(KErrNone);
                }
            break;
            }
        default:
            break;
            }
        }
    void DoCancel() { iTimer.Cancel(); }

    TState iState;
    CLog* iLog;
    RTimer iTimer;
    CMdaAudioOutputStream* iStream;
    TMdaAudioDataSettings iSettings;
    HBufC8* iBuf;
    TPtr8 iBufPtr;
    TInt iT0, iPlayStart, iLastWrite;
    TInt iWritten, iCopies, iCopyErrs, iCopyErr, iAfterStopWrites;
    TBool iInStop;
    TInt iBusy0;
    TBool iPhaseE;
    CKick* iKick;
    TInt iKickStart, iKickCopies0, iKickPhase;
    TWorkerShared* iShared[2];
    RThread iThread[2];
    TInt iARound, iRep, iStartMs;
    TGaps iGaps;
    CStreamProbe* iProbe;
    CBlank* iBlank;
    };

class CNgAppUi : public CAknAppUi
    {
public:
    void ConstructL()
        {
        BaseConstructL();
        iBlank = new (ELeave) CBlank;
        iBlank->ConstructL(ClientRect());
        AddToStackL(iBlank);
        iRunner = CRunner::NewL(iBlank);
        }
    ~CNgAppUi()
        {
        delete iRunner;
        if (iBlank) { RemoveFromStack(iBlank); delete iBlank; }
        }
    void HandleCommandL(TInt aCommand)
        {
        if (aCommand == EEikCmdExit || aCommand == EAknSoftkeyExit || aCommand == EAknSoftkeyBack)
            Exit();
        }
private:
    CBlank* iBlank;
    CRunner* iRunner;
    };

class CNgDocument : public CAknDocument
    {
public:
    CNgDocument(CEikApplication& aApp) : CAknDocument(aApp) {}
    CEikAppUi* CreateAppUiL() { return new (ELeave) CNgAppUi; }
    };

class CNgApplication : public CAknApplication
    {
public:
    TUid AppDllUid() const { return KUidNgTest; }
    CApaDocument* CreateDocumentL() { return new (ELeave) CNgDocument(*this); }
    };

GLDEF_C TInt E32Dll(TDllReason) { return KErrNone; }
EXPORT_C CApaApplication* NewApplication() { return new CNgApplication; }
