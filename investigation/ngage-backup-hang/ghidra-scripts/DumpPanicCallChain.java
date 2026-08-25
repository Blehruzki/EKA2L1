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

// Round 55: live capture shows !ecomserver's panic SVC (thread_kill w/ etype=panic,
// category=E32USER-CBase, reason=47) is called with lr=0x803a9717 - still inside
// euser.dll, not ecomserver.exe. Decompile the function containing 0x803a9717 (the
// real User::Panic()-adjacent framework code) and find its own callers, to trace
// one level further toward whatever ecomserver.exe-side decision actually triggers
// this panic.
public class DumpPanicCallChain extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/panic_call_chain.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        Address target = toAddr(0x803a9717L);
        Function func = fm.getFunctionContaining(target);
        out.printf("containing function of 0x803a9717: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "none");

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        if (func == null) {
            disassemble(target);
            createFunction(target, null);
            func = fm.getFunctionContaining(target);
            out.printf("after forcing: %s%n", func != null ? (func.getName() + "@" + func.getEntryPoint()) : "still none");
        }

        if (func != null) {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            int printed = 0;
            out.println("--- disassembly ---");
            while (ins.hasNext() && printed < 200) {
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

            out.println();
            out.println("=== callers of this function ===");
            Address entry = func.getEntryPoint();
            InstructionIterator allIns = listing.getInstructions(true);
            int count = 0;
            while (allIns.hasNext()) {
                Instruction i = allIns.next();
                for (Address ref : i.getFlows()) {
                    if (ref.equals(entry) || ref.equals(entry.add(1))) {
                        Function cf = fm.getFunctionContaining(i.getAddress());
                        out.printf("caller @ %s (in %s): %s%n", i.getAddress(),
                            cf != null ? cf.getName() + "@" + cf.getEntryPoint() : "???", i.toString());
                        count++;
                    }
                }
            }
            out.printf("total callers: %d%n", count);
        } else {
            out.println("could not resolve function, dumping raw window");
            Address winStart = target.subtract(0x40);
            Address winEnd = target.add(0x100);
            InstructionIterator ins = listing.getInstructions(winStart, true);
            int count = 0;
            while (ins.hasNext() && count < 200) {
                Instruction i = ins.next();
                if (i.getAddress().compareTo(winEnd) > 0) break;
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
