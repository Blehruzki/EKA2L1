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

// Round 52: live capture found cdlserver.exe calls into drtaeabi.dll from exactly
// two return addresses, 0x83485ffb and 0x8348600d, each hit twice with identical
// arguments both times:
//   lr=0x83485ffb r0=0x7006d8 r1=0x0 r2=0x80397398 r3=0x0
//   lr=0x8348600d r0=0xfffffff1 r1=0x0 r2=0x4d5241 r3=0x4d5241
// Decompile the containing function(s) to see what's actually being computed and
// which drtaeabi.dll function is actually invoked at each call site.
public class DumpCdlserverDrtaeabiCallsites extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/cdlserver_drtaeabi_callsites.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        long[] targets = { 0x83485ffbL, 0x8348600dL };

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        for (long t : targets) {
            Address target = toAddr(t);
            out.printf("=== return address %s ===%n", target);

            Function func = fm.getFunctionContaining(target);
            out.printf("containing function: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

            Address winStart = target.subtract(0x30);
            Address winEnd = target.add(0x10);
            out.println("--- disassembly around call site ---");
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 60) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(winEnd) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }

            if (func != null) {
                out.println("--- decompiled function ---");
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
