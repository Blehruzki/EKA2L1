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

public class DumpErrFn extends GhidraScript {
    private static final long[] TARGETS = { 0x803aa0f6L, 0x8039f7dcL };
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/errfn_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();
        for (long t : TARGETS) {
            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            out.printf("=== target 0x%x ===%n", t);
            if (func == null) { out.println("  no containing function found"); out.println(); continue; }
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) { Instruction i = ins.next(); out.printf("%s: %s%n", i.getAddress(), i.toString()); }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) out.println(res.getDecompiledFunction().getC());
            else out.println("decompile failed: " + res.getErrorMessage());
            out.println();
        }
        out.close();
        println("WROTE " + outPath);
    }
}
