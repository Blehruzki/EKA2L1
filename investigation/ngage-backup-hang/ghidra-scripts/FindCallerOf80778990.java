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

// Round 57: FUN_80778990's caller is what determines what runs right after it
// returns normally - the point where cycle 2's fatal Leave actually originates.
public class FindCallerOf80778990 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/caller_of_80778990.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address target = toAddr(0x80778990L);
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        out.println("=== callers of 0x80778990 ===");
        java.util.Set<Address> callerFuncs = new java.util.LinkedHashSet<>();
        InstructionIterator ins = listing.getInstructions(true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            for (Address ref : i.getFlows()) {
                if (ref.equals(target) || ref.equals(target.add(1))) {
                    Function f = fm.getFunctionContaining(i.getAddress());
                    out.printf("%s (in %s): %s%n", i.getAddress(),
                        f != null ? f.getName() + "@" + f.getEntryPoint() : "???", i.toString());
                    if (f != null) callerFuncs.add(f.getEntryPoint());
                }
            }
        }
        out.println();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (Address callerAddr : callerFuncs) {
            Function func = fm.getFunctionContaining(callerAddr);
            out.printf("=== FUNCTION %s @ %s size=%d ===%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
            int printed = 0;
            while (ins2.hasNext() && printed < 150) {
                Instruction i = ins2.next();
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
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
