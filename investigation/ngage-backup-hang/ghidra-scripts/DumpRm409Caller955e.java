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

// Round 48: decompile whatever function contains 0x8029955e (rm-409's one
// static caller into the reachable bafl.dll thunk @0x80557b74). If it has an
// early-return guard before this call, that guard is the concrete condition
// gating whether rm-409 ever talks to !BackupServer at all.
public class DumpRm409Caller955e extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_caller_955e.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        Address target = toAddr(0x8029955eL);

        Function func = fm.getFunctionContaining(target);
        out.printf("function containing target: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

        if (func == null) {
            Address winStart = target.subtract(0x60);
            Address winEnd = target.add(0x100);
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 300) {
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
            while (ins.hasNext() && printed < 600) {
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

        out.close();
        println("WROTE " + outPath);
    }
}
