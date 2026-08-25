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

// baksrvs.exe's fallback path calls a fixed address (0x821b6444) that falls in a
// gap Ghidra's xref-based function search never reached from inside baksrv.dll
// itself (only the external caller in baksrvs.exe references it). Force
// disassembly/function creation right there in both ARM and Thumb and see which
// one produces something sane - same technique as DumpBaksrvsTail.java.
public class DumpBaksrvDllTarget extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/baksrv_dll_target_dump.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Address regionStart = toAddr(0x821b6420L);
        Address regionEnd = toAddr(0x821b6480L);

        Address t = toAddr(0x821b6444L);

        clearListing(regionStart, regionEnd);

        ProgramContext ctx = currentProgram.getProgramContext();
        Register reg = ctx.getRegister("TMode");
        if (reg != null) {
            ctx.setValue(reg, regionStart, regionEnd, BigInteger.ONE);
            out.println("TMode set to 1 from " + regionStart + " to " + regionEnd);
        }

        disassemble(regionStart);
        createFunction(toAddr(0x821b643cL), "target_thumb_0x821b643c");
        analyzeAll(currentProgram);

        Listing listing = currentProgram.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        Function func = getFunctionAt(toAddr(0x821b643cL));
        out.println("=== Thumb attempt at 0x821b643c (region 0x821b6420-0x821b6480) ===");
        if (func == null) {
            out.println("no function created");
        } else {
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            while (ins.hasNext()) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
            }
            DecompileResults res = decomp.decompileFunction(func, 60, monitor2);
            out.println("--- decompiled ---");
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
        }

        out.println();
        out.println("=== raw bytes around 0x821b6444 (context) ===");
        Address ctxStart = t.subtract(0x10);
        byte[] buf = new byte[0x60];
        int n = currentProgram.getMemory().getBytes(ctxStart, buf);
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) {
            if (i % 16 == 0) sb.append(String.format("%n%s: ", ctxStart.add(i)));
            sb.append(String.format("%02x", buf[i] & 0xff));
        }
        out.println(sb.toString());

        out.close();
        println("WROTE " + outPath);
    }
}
