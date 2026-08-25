import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileEntry extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/playserver_entry_r60.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Address entry = toAddr(0x70000000L);
        Function f = fm.getFunctionContaining(entry);
        out.printf("=== entry point 0x70000000 ===%n");
        if (f == null) {
            out.printf("no function contains entry point directly - disassembling raw at that address instead%n%n");
            Instruction i = listing.getInstructionAt(entry);
            int cnt = 0;
            while (i != null && cnt < 60) {
                out.printf("  %s: %s%n", i.getAddress(), i);
                i = i.getNext();
                cnt++;
            }
        } else {
            out.printf("function %s @ %s size=%d%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());
            InstructionIterator instrs = listing.getInstructions(f.getBody(), true);
            while (instrs.hasNext()) {
                Instruction i = instrs.next();
                out.printf("  %s: %s%n", i.getAddress(), i);
            }
            DecompileResults res = decomp.decompileFunction(f, 30, new ConsoleTaskMonitor());
            if (res.decompileCompleted()) {
                out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
            } else {
                out.printf("decompile failed: %s%n", res.getErrorMessage());
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
