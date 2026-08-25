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

// Round 48: 0x80299554 is rm-409's lazy-singleton create-and-register wrapper
// (unconditional once reached - no guard of its own). Find who calls it - that
// caller is where the real gating condition (if any) would live.
public class FindRm409Caller99554Callers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/rm409_caller_99554_callers.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address thunk = toAddr(0x80299554L);
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        InstructionIterator ins = listing.getInstructions(true);
        int count = 0;
        while (ins.hasNext()) {
            Instruction i = ins.next();
            for (Address ref : i.getFlows()) {
                if (ref.equals(thunk)) {
                    Function f = fm.getFunctionContaining(i.getAddress());
                    out.printf("caller @ %s (in %s): %s%n", i.getAddress(),
                        f != null ? f.getName() + "@" + f.getEntryPoint() : "???", i.toString());
                    count++;
                }
            }
        }
        out.printf("total callers: %d%n%n", count);

        if (count > 0) {
            // Dump+decompile the containing function of the first caller found.
            ins = listing.getInstructions(true);
            Address callerAddr = null;
            outer:
            while (ins.hasNext()) {
                Instruction i = ins.next();
                for (Address ref : i.getFlows()) {
                    if (ref.equals(thunk)) {
                        callerAddr = i.getAddress();
                        break outer;
                    }
                }
            }

            Function func = fm.getFunctionContaining(callerAddr);
            if (func != null) {
                out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
                DecompInterface decomp = new DecompInterface();
                decomp.openProgram(currentProgram);
                ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();
                DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
                if (res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("decompile failed: " + res.getErrorMessage());
                }
            } else {
                out.println("no function boundary around caller, dumping raw disasm window");
                Address winStart = callerAddr.subtract(0x60);
                Address winEnd = callerAddr.add(0x100);
                InstructionIterator ins2 = listing.getInstructions(winStart, true);
                int printed = 0;
                while (ins2.hasNext() && printed < 200) {
                    Instruction i = ins2.next();
                    if (i.getAddress().compareTo(winEnd) > 0) break;
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                    printed++;
                }
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
