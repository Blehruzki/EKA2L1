import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileAllPlayserver extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/playserver_decompile_all.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        DecompInterface decomp = new DecompInterface();
        decomp.setOptions(new DecompileOptions());
        decomp.openProgram(currentProgram);

        FunctionIterator fiter = currentProgram.getListing().getFunctions(true);
        int count = 0;
        while (fiter.hasNext() && count < 300) {
            Function f = fiter.next();
            count++;
            out.println("==== FUNCTION " + f.getName() + " @ " + f.getEntryPoint() + " ====");
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
        println("WROTE " + outPath + " (" + count + " functions)");
    }
}
