import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.ProgramContext;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.math.BigInteger;

// eikbackupsrv.dll's primary CSession2-style vtable (found at 0x82148150, referenced
// from the object's offset 0) and secondary interface vtable (offset 0x78, at
// 0x82148190) - dump every unique function pointer entry, looking for the real
// ServiceL/message-dispatch function (the one comparing against small message
// opcodes like 0x1f/0x20/0x22, matching the historical ord 31/32/34 sequence).
public class DumpVtableFns extends GhidraScript {
    private static final long[] TARGETS = {
        0x82147413L, 0x8214743dL, 0x82147a68L, 0x82147950L, 0x82147948L,
        0x82147c70L, 0x82147958L, 0x8214772fL, 0x82147c60L, 0x82147c30L,
        0x8214744dL, 0x821476b7L, 0x82147715L, 0x82147757L,
        0x82147775L, 0x82147a70L, 0x82147c20L, 0x82147990L
    };

    @Override
    protected void run() throws Exception {
        String outPath = "C:/Users/Usuario/AppData/Local/Temp/claude/C--Users-Usuario-Desktop-EKA2L1/bfc67f55-2604-45fa-8c6b-63b6e44854cc/scratchpad/vtable_fns_result.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor2 = new ConsoleTaskMonitor();

        ProgramContext ctx = currentProgram.getProgramContext();
        Register tmodeReg = ctx.getRegister("TMode");

        int idx = 0;
        for (long t : TARGETS) {
            idx++;
            Address addr = toAddr(t);
            Function func = fm.getFunctionContaining(addr);
            String forceErr = null;
            if (func == null) {
                try {
                    clearListing(addr, addr.add(0x100));
                    if (tmodeReg != null) {
                        ctx.setValue(tmodeReg, addr, addr.add(0x100), BigInteger.ONE);
                    }
                    disassemble(addr);
                    createFunction(addr, "vfn_" + addr);
                    func = fm.getFunctionContaining(addr);
                } catch (Exception e) {
                    forceErr = e.toString();
                }
            }

            out.printf("=== vtable[%d] 0x%x ===%n", idx, t);
            if (func == null) {
                out.println("  no containing function found; forceErr=" + forceErr);
                out.println();
                continue;
            }
            out.printf("FUNCTION %s @ %s size=%d%n", func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses());
            out.println("--- disassembly ---");
            InstructionIterator ins = listing.getInstructions(func.getBody(), true);
            int count = 0;
            while (ins.hasNext() && count < 80) {
                Instruction i = ins.next();
                out.printf("%s: %s%n", i.getAddress(), i.toString());
                count++;
            }
            out.println("--- decompiled ---");
            DecompileResults res = decomp.decompileFunction(func, 30, monitor2);
            if (res.decompileCompleted()) {
                out.println(res.getDecompiledFunction().getC());
            } else {
                out.println("decompile failed: " + res.getErrorMessage());
            }
            out.println();
        }

        out.close();
        println("WROTE " + outPath);
    }
}
