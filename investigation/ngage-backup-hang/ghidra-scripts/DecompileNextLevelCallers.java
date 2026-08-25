import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileNextLevelCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/next_level_callers_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = { 0x82062f6cL, 0x820633d2L, 0x82063608L };

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            out.println("==== " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)") + " ====");
            if (f == null) continue;
            try {
                DecompileResults res = decomp.decompileFunction(f, 30, monitor);
                if (res != null && res.decompileCompleted()) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("(decompile failed)");
                }
            } catch (Exception e) {
                out.println("(exception: " + e.getMessage() + ")");
            }
            out.println();
        }

        // Inspect what's around the unresolved caller 0x82063776 - raw disasm context
        out.println("==== context around 0x82063776 (unresolved caller of FUN_8206330e) ====");
        Address ctxAddr = toAddr(0x82063750L);
        Instruction instr = getInstructionAt(ctxAddr);
        for (int i = 0; i < 30 && instr != null; i++) {
            out.printf("%s: %s%n", instr.getAddress(), instr.toString());
            instr = instr.getNext();
        }

        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
