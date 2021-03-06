"""
 * FEASST - Free Energy and Advanced Sampling Simulation Toolkit
 * http:#pages.nist.gov/feasst, National Institute of Standards and Technology
 * Harold W. Hatch, harold.hatch@nist.gov
 *
 * Permission to use this data/software is contingent upon your acceptance of
 * the terms of LICENSE.txt and upon your providing
 * appropriate acknowledgments of NIST's creation of the data/software.
"""

import unittest
import feasst

class TestSPCE_SRSW_NVTMC(unittest.TestCase):
    def test(self):
        feasst.ranInitByDate()
        space = feasst.makeSpace(feasst.args(
         {"dimen" : "3",
          "boxLength" : "24.8586887"}))

        pair = feasst.makePairLJCoulEwald(space, feasst.args(
         {"rCut" : str(space.minl()/2.),
          "molTypeInForcefield" : "data.spce",
          "alphaL" : "5.6",
          "k2max" : "38"}));

        # acceptance criteria
        temperature = 298  # Kelvin
        beta = 1./(temperature*feasst.idealGasConstant/1e3)  # mol/KJ
        criteria = feasst.CriteriaMetropolis(beta, 1.)
        mc = feasst.MC(space, pair, criteria)
        feasst.transformTrial(mc, "translate", 0.1)
        feasst.transformTrial(mc, "rotate", 0.1)
        mc.initLog("log", int(1e4))
        mc.initMovie("movie", int(1e4))
        mc.initRestart("tmp/rst", int(1e4))
        mc.setNFreqTune(int(1e4))
        mc.setNFreqCheckE(int(1e4), 1e-6);
        mc.nMolSeek(512);
        mc.runNumTrials(int(1e6))   # run equilibration

        # Run the production simulation
        mc.initProduction();
        mc.zeroStat();
        mc.setNFreqTune(0);
        mc.runNumTrials(int(1e6))

        # Check average energy against Gerhard Hummer
        # https:#doi.org/10.1063/1.476834
        peAv = mc.peAccumulator().average()/float(space.nMol())
        peStd = mc.peAccumulator().blockStdev()/float(space.nMol())
        pePublish = -46.82  # published value
        pePublishStd = 0.02
        self.assertAlmostEqual(peAv, pePublish, delta=2.576*(pePublishStd+peStd))

if __name__ == "__main__":
    unittest.main()
