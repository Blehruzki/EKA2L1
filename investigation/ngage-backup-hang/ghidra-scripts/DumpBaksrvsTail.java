import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// The bootstrap functions found so far (ARM mode) stop cold at 0x821b7518 -
// the bytes there look like a short, real Thumb function (10b5 = push
// {r4,lr}, ... 10bd = pop {r4,pc}). Force Thumb mode from there to the end
// of the block and see what's actually there.
public class DumpBaksrvsTail extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/baksrvs_tail_dump.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address tailStart = toAddr(0x821b7518L);
        Address blockEnd = currentProgram.getMemory().getBlock(tailStart).getEnd();

        ProgramContext ctx = currentProgram.getProgramContext();
        Register reg = ctx.getRegister("TMode");
        if (reg != null) {
            ctx.setValue(reg, tailStart, blockEnd, BigInteger.ONE);
            out.println("TMode set to 1 from " + tailStart + " to " + blockEnd);
        } else {
            out.println("WARNING: no TMode register");
        }

        clearListing(tailStart, blockEnd);
        disassemble(tailStart);
        createFunction(tailStart, "tail_" + tailStart);
        analyzeAll(currentProgram);

        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        FunctionIterator fit = listing.getFunctions(tailStart, true);
        int count = 0;
        while (fit.hasNext()) {
            Function func = fit.next();
            count++;
            out.printf("=== FUNCTION %s @ %s size=%d ===%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
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
        }
        out.println("TOTAL FUNCTIONS: " + count);
        out.close();
        println("WROTE " + outPath + " funcs=" + count);
    }
}
