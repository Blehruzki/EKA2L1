import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.MemoryBlock;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// Round 20 (following the user's chosen direction after round 19 closed the
// dispatch mechanism: check whether other devices' baksrv.dll ever fills in the
// analogous case). rm-320's baksrv.dll has a switch dispatcher whose case for
// opcode 32 (case index 0xc/12) compiles to nothing but the shared no-op return.
// rm-409's baksrv.dll is nominally the same DLL version (2.1, build 0x240) but
// nearly 2x the code size with more exports - a real candidate for "does a fuller
// build implement this case." Force Thumb (established default for this codebase)
// across the whole imported block, run full auto-analysis (small file, cheap) and
// dump every function found, to locate anything resembling the same
// switch/jump-table ServiceL dispatcher pattern.
public class DumpBaksrvRm409Overview extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/baksrv_rm409_overview.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        out.println("=== memory blocks ===");
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            out.printf("  %s: %s - %s executable=%s%n", b.getName(), b.getStart(), b.getEnd(), b.isExecute());
            if (tmodeReg != null) {
                ctx.setValue(tmodeReg, b.getStart(), b.getEnd(), BigInteger.ONE);
            }
        }
        out.println();

        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            disassemble(b.getStart());
        }
        analyzeAll(currentProgram);

        Listing listing = currentProgram.getListing();
        FunctionIterator fit = listing.getFunctions(true);
        int count = 0;
        while (fit.hasNext()) {
            Function func = fit.next();
            count++;
            out.printf("FUNC %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
        }
        out.println("TOTAL FUNCTIONS: " + count);

        out.close();
        println("WROTE " + outPath + " funcs=" + count);
    }
}
