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

// Round 52 follow-up: confirm/deny the exception-handling-cleanup hypothesis for
// FUN_80395580 (calls 0x80395370, 0x803953a8, 0x803953de, 0x80395510) and
// FUN_803954e8 (calls 0x803954a6, 0x80394bae).
public class DumpDrtaeabiSubCallees extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/drtaeabi_sub_callees.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        long[] targets = { 0x80395370L, 0x803953a8L, 0x803953deL, 0x80395510L, 0x803954a6L, 0x80394baeL };

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long t : targets) {
            Address target = toAddr(t);
            out.printf("=== target %s ===%n", target);

            Function func = fm.getFunctionContaining(target);
            if (func == null) {
                disassemble(target);
                createFunction(target, null);
                func = fm.getFunctionContaining(target);
            }

            if (func != null) {
                out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
                InstructionIterator ins = listing.getInstructions(func.getBody(), true);
                int printed = 0;
                out.println("--- disassembly ---");
                while (ins.hasNext() && printed < 120) {
                    Instruction i = ins.next();
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                    printed++;
                }

                DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
                out.println("--- decompiled ---");
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
            } else {
                out.println("could not resolve function boundary");
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
