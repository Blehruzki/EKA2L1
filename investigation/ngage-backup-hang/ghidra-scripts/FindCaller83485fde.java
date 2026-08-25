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

// Round 53: round 52 confirmed 0x83485fde is cdlserver.exe's own wrapper directly
// above the exception-cleanup call chain into drtaeabi.dll. Find every caller of
// 0x83485fde and decompile the containing function(s) - this is the real locus of
// the repeated throw/catch cycle: whatever calls this wrapper repeatedly, and why.
public class FindCaller83485fde extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/caller_83485fde.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address target = toAddr(0x83485fdeL);
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        out.println("=== callers of 0x83485fde ===");
        java.util.Set<Address> callerFuncs = new java.util.LinkedHashSet<>();
        InstructionIterator ins = listing.getInstructions(true);
        while (ins.hasNext()) {
            Instruction i = ins.next();
            for (Address ref : i.getFlows()) {
                if (ref.equals(target)) {
                    Function f = fm.getFunctionContaining(i.getAddress());
                    out.printf("%s (in %s): %s%n", i.getAddress(),
                        f != null ? f.getName() + "@" + f.getEntryPoint() : "???", i.toString());
                    if (f != null) {
                        callerFuncs.add(f.getEntryPoint());
                    } else {
                        callerFuncs.add(i.getAddress());
                    }
                }
            }
        }
        out.println();

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (Address callerAddr : callerFuncs) {
            Function func = fm.getFunctionContaining(callerAddr);
            if (func == null) {
                out.printf("=== no function at %s, raw window ===%n", callerAddr);
                Address winStart = callerAddr.subtract(0x40);
                Address winEnd = callerAddr.add(0x100);
                InstructionIterator ins2 = listing.getInstructions(winStart, true);
                int count = 0;
                while (ins2.hasNext() && count < 200) {
                    Instruction i = ins2.next();
                    if (i.getAddress().compareTo(winEnd) > 0) break;
                    out.printf("%s: %s%n", i.getAddress(), i.toString());
                    count++;
                }
                out.println();
                continue;
            }

            out.printf("=== FUNCTION %s @ %s size=%d ===%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
            int printed = 0;
            out.println("--- disassembly ---");
            while (ins2.hasNext() && printed < 300) {
                Instruction i = ins2.next();
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
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
