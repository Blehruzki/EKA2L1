import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileApgrfxCallers extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/apgrfx_callers_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = {
            0x820670e2L, 0x8206712eL, 0x8206719cL, 0x820671a0L, 0x8206722eL,
            0x82067232L, 0x82067258L, 0x8206732aL, 0x8206738cL, 0x820673f0L,
            0x82067678L, 0x820676e6L, 0x8206780aL, 0x8206785cL, 0x82067918L,
            0x82067af4L, 0x82067c1eL, 0x82067e1cL, 0x82067ea4L, 0x82067f62L,
            0x82067f78L, 0x82068040L, 0x82068160L, 0x8206822cL, 0x8206832aL,
            0x82068380L, 0x820683dcL, 0x82068494L, 0x820684e2L, 0x8206862cL,
            0x820687e4L, 0x820688aaL, 0x820688dcL, 0x82068944L, 0x82068b22L,
            0x82068b3cL, 0x82068cdaL, 0x82068d60L, 0x82068e30L
        };

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        for (long target : targets) {
            Address a = toAddr(target);
            Function f = getFunctionContaining(a);
            if (f == null) {
                out.println("==== 0x" + Long.toHexString(target) + " (no function) ====");
                continue;
            }
            out.println("==== " + f.getName() + " @ " + f.getEntryPoint() + " ====");
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
