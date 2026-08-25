import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

// Round 50: round 25 identified FUN_8077811a (connect-and-one-call) and
// FUN_80778190 (calls FUN_8077811a in a loop capped at 10 iterations,
// breaking out early only on specific result codes). Decompile both fully
// this time to find exactly which error code(s) make the loop break early -
// that's the value EKA2L1 would need to complete the stuck message with for
// bafl.dll's own retry logic to terminate gracefully instead of retrying
// forever (round 21's rejected KErrNone) or hanging forever (rounds 1-48).
public class DumpFun80778190Retry extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/fun80778190_retry.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        long[] targets = { 0x8077811aL, 0x80778190L };

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long t : targets) {
            Address target = toAddr(t);
            Function func = fm.getFunctionContaining(target);
            out.printf("=== target %s -> function %s ===%n", target,
                func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

            if (func == null) {
                Address winStart = target.subtract(0x20);
                Address winEnd = target.add(0x200);
                InstructionIterator ins = listing.getInstructions(winStart, true);
                int count = 0;
                while (ins.hasNext() && count < 400) {
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

        out.close();
        println("WROTE " + outPath);
    }
}
