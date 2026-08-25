import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileBigDispatch extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/big_dispatch_r70.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        decomp.setOptions(new ghidra.app.decompiler.DecompileOptions());

        Address a = toAddr(0x70803998L);
        Function f = fm.getFunctionContaining(a);
        out.printf("function %s @ %s size=%d bytes%n%n", f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses());

        DecompileResults res = decomp.decompileFunction(f, 120, new ConsoleTaskMonitor());
        if (res.decompileCompleted()) {
            out.printf("--- decompiled ---%n%s%n", res.getDecompiledFunction().getC());
        } else {
            out.printf("decompile failed: %s%n", res.getErrorMessage());
        }

        out.close();
        println("WROTE " + outPath);
    }
}
