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
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// Redo of round 7's DumpCompareFn2, now against the CORRECTED euser.dll extraction
// (the old one was off by 36 bytes - see round 8 of investigation/ngage-backup-hang
// /README.md). 0x803a7cf2 is baksrvs.exe's real-vs-fallback branch condition call.
public class DumpCompareFn3 extends GhidraScript {
    private static final long START = 0x803a7cf2L;

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/euser_comparefn_fixed_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address startAddr = toAddr(START);
        clearListing(startAddr, startAddr.add(0x80));
        disassemble(startAddr);
        createFunction(startAddr, "target_" + startAddr);
        analyzeAll(currentProgram);

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        List<Long> toDump = new ArrayList<>();
        toDump.add(START);
        Set<Long> seen = new HashSet<>();
        int idx = 0;
        while (idx < toDump.size() && idx < 8) {
            long t = toDump.get(idx);
            idx++;
            if (!seen.add(t)) continue;

            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            out.printf("=== target 0x%x ===%n", t);
            if (func == null) {
                out.println("  no containing function found");
                out.println();
                continue;
            }
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();

            InstructionIterator ins2 = listing.getInstructions(func.getBody(), true);
            while (ins2.hasNext()) {
                Instruction i = ins2.next();
                String mnem = i.getMnemonicString();
                if (mnem != null && (mnem.startsWith("bl") || mnem.equals("blx"))) {
                    for (Address flow : i.getFlows()) {
                        toDump.add(flow.getOffset());
                    }
                }
            }
        }

        out.close();
        println("WROTE " + outPath);
    }
}
