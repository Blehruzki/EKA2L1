import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileGetAppInfoPublic extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/getappinfo_public_decompile.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        long[] targets = { 0x82062e26L, 0x8206330eL };

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

            // Also check who calls this one (is it exported / called from outside apgrfx.dll?)
            ghidra.program.model.symbol.ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(a);
            int count = 0;
            while (refs.hasNext()) {
                ghidra.program.model.symbol.Reference r = refs.next();
                Address fromAddr = r.getFromAddress();
                Function cf = getFunctionContaining(fromAddr);
                out.printf("  caller: 0x%s in %s%n", fromAddr, (cf != null ? cf.getName() + " @ " + cf.getEntryPoint() : "(none)"));
                count++;
            }
            out.printf("  total callers within this program: %d%n", count);
            out.println();
        }

        decomp.dispose();
        out.close();
        println("WROTE " + outPath);
    }
}
