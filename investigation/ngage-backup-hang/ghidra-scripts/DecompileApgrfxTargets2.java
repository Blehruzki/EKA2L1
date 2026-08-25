import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileApgrfxTargets2 extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/apgrfx_targets2_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = { 0x8206706eL, 0x82068875L, 0x8206edacL, 0x82066f00L };

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        java.util.Set<Function> seen = new java.util.HashSet<>();
        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            out.println("==== target 0x" + Long.toHexString(target) + " -> " + (f != null ? f.getName() + " @ " + f.getEntryPoint() : "(none)") + " ====");
            if (f == null || seen.contains(f)) {
                continue;
            }
            seen.add(f);
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

        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
