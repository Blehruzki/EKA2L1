import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 34: round 33's live probe caught CdlServer with pc=0x803946b8, lr=0x80394bef,
// both inside drtaeabi.dll's confirmed spin range (0x8039456c-0x80395728). Dump the
// function(s) at both addresses to see what division/arithmetic algorithm this
// actually is and whether it has a genuine termination bug.
public class DumpDrtaeabiDivision extends GhidraScript {
    private void dumpFunctionAt(PrintWriter out, Listing listing, FunctionManager fm,
            Address target, Address winStart, Address winEnd, ProgramContext ctx, Register tmodeReg) throws Exception {
        clearListing(winStart, winEnd);
        if (tmodeReg != null) {
            ctx.setValue(tmodeReg, winStart, winEnd, BigInteger.ONE);
        }
        disassemble(winStart);

        Function func = fm.getFunctionContaining(target);
        out.printf("=== function containing 0x%s ===%n", target);
        if (func == null) {
            out.println("no function found; dumping raw window instead");
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 200) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(winEnd) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
        } else {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            int printed = 0;
            while (ins.hasNext() && printed < 400) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                printed++;
            }

            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
        }
        out.println();
    }

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/drtaeabi_division.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // pc at capture time - the innermost, currently-executing routine.
        dumpFunctionAt(out, listing, fm, toAddr(0x803946b8L), toAddr(0x80394600L), toAddr(0x80394d00L), ctx, tmodeReg);

        // lr at capture time - the caller, one frame up.
        dumpFunctionAt(out, listing, fm, toAddr(0x80394befL), toAddr(0x80394b00L), toAddr(0x80395000L), ctx, tmodeReg);

        // sp+0x1c / sp+0x24 / sp+0x34 from the same capture - further frames up.
        dumpFunctionAt(out, listing, fm, toAddr(0x80395343L), toAddr(0x80395200L), toAddr(0x80395728L), ctx, tmodeReg);
        dumpFunctionAt(out, listing, fm, toAddr(0x80394acfL), toAddr(0x80394a00L), toAddr(0x80394c00L), ctx, tmodeReg);
        dumpFunctionAt(out, listing, fm, toAddr(0x80394b7fL), toAddr(0x80394b00L), toAddr(0x80394d00L), ctx, tmodeReg);

        out.close();
        println("WROTE " + outPath);
    }
}
