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

static void FillTone(TDes8& aBuf, TInt aPhase)
    {
    // A quiet square wave at 400 Hz: no floating point, no tables.
    aBuf.SetLength(KBufBytes);
    TUint16* p = (TUint16*)aBuf.Ptr();
    for (TInt i = 0; i < KBufSamples; i++)
        p[i] = (TUint16)((((aPhase + i) / 20) & 1) ? 1500 : (TUint16)-1500);
    }

static TInt NowMs() { TTime t; t.UniversalTime(); return (t.Int64() / TInt64(1000)).Low(); }

static TInt64 PositionUs(CMdaAudioOutputStream* aStream)
    {
    return aStream->Position().Int64();
    }

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
        if (aError == KErrNone && !iStopped) { iShared->iBuffers++; Write(); }
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
// The main thread's runner: one active object, a state machine of phases.
class CRunner : public CActive, public MMdaAudioOutputStreamCallback
    {
public:
    enum TState { EIdle, EAOpen, EAPlay, EAStop, EAStarve, EBDone, ECWarm, ECOneBusy, ECWaitStop, ECStopBusy,
                  ECJoin, EDWarm, EDBusy, EDJoin, EEOpen, EEKick, EEKick2, EEEnd, EDone };
    static CRunner* NewL()
        {
        CRunner* self = new (ELeave) CRunner;
        CleanupStack::PushL(self);
        self->ConstructL();
        CleanupStack::Pop(self);
        return self;
        }
    ~CRunner()
        {
        Cancel();
        iTimer.Close();
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
        iLog->Line(_L("A open-complete err=%d after %d ms"), aError, Ms() - iT0);
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
        TInt64 pos = PositionUs(iStream);
        TInt played = (TInt)(pos / TInt64(1000)).Low() * (KRate / 1000);
        if ((iCopies % 4) == 1 && !iPhaseE)
            iLog->Line(_L("A copied #%d at %d ms (fast %u): written %d played %d ahead %d"),
                iCopies, Ms() - iPlayStart, User::FastCounter(), iWritten, played, iWritten - played);
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
        iLog->Line(_L("A play-complete err=%d inside-stop=%d at %d ms"), aError, iInStop, Ms() - iPlayStart);
        if (!iInStop && iState == EAStarve)
            {
            iLog->Line(_L("A underflow came %d ms after the last write"), Ms() - iLastWrite);
            Next(EBDone, 200);
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
        iLog->Line(_L("ngtest 1: FastCounter %u, TickCount %u"), User::FastCounter(), User::TickCount());
        iT0 = Ms();
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
        iLog->Line(_L("A Stop took %d ms (%u fast ticks); copies %d, errors %d (last %d)"),
            Ms() - t0, User::FastCounter() - f0, iCopies, iCopyErrs, iCopyErr);
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
        iLog->Line(_L("worker %d: open err=%d buffers=%d stop %d ms (%u fast ticks), play-complete err=%d inside-stop=%d done=%d"),
            aIndex, s->iOpenErr, s->iBuffers, s->iStopEndMs - s->iStopStartMs,
            s->iStopEndTick - s->iStopStartTick, s->iPlayCompleteErr, s->iPlayCompleteInStop, s->iDone);
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
            iLog->Line(_L("A no play-complete after starving; going on"));
            Next(EBDone, 10);
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
            StartWorker(1, 6000);
            iThread[0].Close();
            delete iShared[0];
            iShared[0] = NULL;
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
            iLog->Line(_L("E frame loop, main stream playing: %d frames in %d ms, %d buffers copied"), iKick->iRuns, t, iCopies - iKickCopies0);
            iThread[0].Close();
            delete iShared[0];
            iShared[0] = NULL;
            iThread[1].Close();
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
                iKick->Go();
                Next(EEKick2, 3000);
                }
            else
                {
                iKick->Halt();
                TInt t = Ms() - iKickStart;
                iLog->Line(_L("E frame loop, main stream and a worker's playing: %d frames in %d ms, %d buffers copied"), iKick->iRuns, t, iCopies - iKickCopies0);
                Next(EEEnd, 2000);
                }
            break;
        case EEEnd:
            {
            if (iStream) iStream->Stop();
            LogWorker(0);
            // Phase F.
            TSpinShared* sp = new TSpinShared;
            sp->iStop = 0;
            sp->iCount = 0;
            sp->iSeenStop = -1;
            sp->iArg = 0;
            RThread spin;
            TInt err = spin.Create(_L("ngspin"), SpinMain, 0x2000, 0x1000, 0x10000, sp);
            if (err == KErrNone)
                {
                TRequestStatus st;
                spin.Logon(st);
                iLog->Line(_L("F priorities: main %d, spinner %d"), (TInt)RThread().Priority(), (TInt)spin.Priority());
                TInt t0 = NowMs();
                spin.Resume();
                TInt t1 = NowMs();
                User::After(100000);        // let it through its startup and into the loop
                TInt c0 = sp->iCount;
                TInt n = Busy(1000);
                TInt c1 = sp->iCount;
                TInt t2 = NowMs();
                sp->iStop = 1;
                User::WaitForRequest(st);
                iLog->Line(_L("F one second, main busy and a same-priority spinner: main %d iterations (%d%% of B), spinner %d"),
                    n, Pct(n, iBusy0), sp->iCount);
                TExitCategoryName cat = spin.ExitCategory();
                iLog->Line(_L("F spinner: arg %08x (shared %08x), stop seen at entry %d, exit type %d reason %d category %S"),
                    (TUint32)sp->iArg, (TUint32)sp, sp->iSeenStop, (TInt)spin.ExitType(), spin.ExitReason(), &cat);
                iLog->Line(_L("F times from Resume: Resume returned %d ms, busy ended %d ms, spinner entered %d ms"),
                    t1 - t0, t2 - t0, sp->iEntryMs - t0);
                iLog->Line(_L("F spinner count: %d after the 100 ms sleep, %d after the busy second (+%d during it)"),
                    c0, c1, c1 - c0);
                spin.Close();
                }
            else
                iLog->Line(_L("F spinner not created: %d"), err);
            delete sp;
            iLog->Line(_L("DONE"));
            iState = EDone;
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
    };

// ---------------------------------------------------------------------------
// The application, minimal: a black control and the runner.
class CBlank : public CCoeControl
    {
public:
    void ConstructL(const TRect& aRect) { CreateWindowL(); SetRect(aRect); ActivateL(); }
    void Draw(const TRect&) const
        {
        CWindowGc& gc = SystemGc();
        gc.SetBrushColor(KRgbBlack);
        gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
        gc.Clear(Rect());
        }
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
        iRunner = CRunner::NewL();
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
