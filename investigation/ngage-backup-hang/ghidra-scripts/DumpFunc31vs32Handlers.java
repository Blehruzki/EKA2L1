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

// Round 15's live single-step diff found that function=31 (completes) and
// function=32 (silently dropped, the N-Gage hang) execute byte-for-byte identical
// code for their first 99 instructions of dispatch, then split at two addresses
// only 8 bytes apart - 0x821b6226 (func=31's handler) and 0x821b622e (func=32's,
// the buggy one). Force Thumb + disassemble (not full decompile, which has been
// unreliable on this binary every prior round) a generous window around both so
// they can be read and compared side by side.
public class DumpFunc31vs32Handlers extends GhidraScript {
    private static final long[] TARGETS = { 0x821b6226L, 0x821b622eL };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/func31_vs_32_handlers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        for (long t : TARGETS) {
            Address addr = toAddr(t);
            Address regionEnd = addr.add(0x120);

            out.printf("=== target 0x%x ===%n", t);

            try {
                clearListing(addr, regionEnd);
                if (tmodeReg != null) {
                    ctx.setValue(tmodeReg, addr, regionEnd, BigInteger.ONE);
                }
                disassemble(addr);
                createFunction(addr, "handler_" + Long.toHexString(t));
            } catch (Exception e) {
                out.println("  disassemble/createFunction failed: " + e);
            }

            Function func = fm.getFunctionContaining(addr);
            if (func == null) {
                out.println("  no containing function found");
                out.println();
                continue;
            }

            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            int count = 0;
            while (ins.hasNext() && count < 120) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }

            out.println("--- decompiled (best-effort) ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
