/*
 * FEASST - Free Energy and Advanced Sampling Simulation Toolkit
 * http://pages.nist.gov/feasst, National Institute of Standards and Technology
 * Harold W. Hatch, harold.hatch@nist.gov
 *
 * Permission to use this data/software is contingent upon your acceptance of
 * the terms of LICENSE.txt and upon your providing
 * appropriate acknowledgments of NIST's creation of the data/software.
 */

#include "./trial_confswap_txt.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace feasst {

TrialConfSwapTXT::TrialConfSwapTXT() : Trial() {
  defaultConstruction_();
}

TrialConfSwapTXT::TrialConfSwapTXT(
  Pair *pair,
  Criteria *criteria)
  : Trial(pair, criteria) {
  defaultConstruction_();
}

TrialConfSwapTXT::TrialConfSwapTXT(const char* fileName,
  Pair *pair,
  Criteria *criteria)
  : Trial(pair, criteria, fileName) {
  defaultConstruction_();
}

void TrialConfSwapTXT::writeRestart(const char* fileName) {
  writeRestartBase(fileName);
  std::ofstream file(fileName, std::ios_base::app);
}

void TrialConfSwapTXT::defaultConstruction_() {
  className_.assign("TrialConfSwapTXT");
  trialType_.assign("move");
  verbose_ = 0;
  struct stat st = {0};
  if (stat("tmp", &st) == -1) {
    mkdir("tmp", 0700);
  }
}

void TrialConfSwapTXT::attempt1_() {
  // obtain currentOrder, the current order parameter for the simulation
  double currentOrder = -1;
  if (orderType_.compare("nmol") == 0) {
    currentOrder = space()->nMol();
  } else if (orderType_.compare("pairOrder") == 0) {
    currentOrder = pair_->order();
  } else {
    ASSERT(0, "unrecognized macrostate type (" << orderType_ << ")");
  }

  // obtain procIndex, neighboring processors which overlap with the same order
  // parameter
  vector<int> procIndex;
  for (unsigned int i = 0; i < order_.size(); ++i) {
    if (fabs(currentOrder - order_[i]) < DTOL) {
      procIndex.push_back(i);
    }
  }

  // if no processor overlaps, skip trial and don't count the attempt for trial
  // statistics
  if (procIndex.size() == 0) {
    --attempted_;
  } else {
    // randomly choose one of the processor indices to attempt a swap or store
    const int index = procIndex[uniformRanNum(0, procIndex.size() - 1)];

    // 1/2 chance to store current configuration by writing a file
    if (uniformRanNum() < 0.5) {
      // write current configuration but reject move
      stringstream ss;
      ss << "tmp/swpp" << proc_ << "p" << process_[index] << "o"
         << currentOrder;
      space()->writeRestart(ss.str().c_str());
      if (nLines_[index] == 0) nLines_[index] = numLines(ss.str());
      --attempted_;

    // 1/2 chance to swap configurations on this processor with stored
    // configuration from other processor
    } else {
      stringstream ss;
      ss << "tmp/swpp" << process_[index] << "p" << proc_ << "o"
         << currentOrder;
      if (!fileExists(ss.str().c_str())) {
        --attempted_;
      } else {
        // copy file
        std::ifstream src(ss.str().c_str(), std::fstream::binary);
        ss << "cpy";
        { std::ofstream dst(ss.str().c_str(),
          std::fstream::trunc|std::fstream::binary);
          dst << src.rdbuf();
        }

        // test number of lines are correct
        const int n = numLines(ss.str());
        if ( (n != nLines_[index]) || (n == 0) ) {
          --attempted_;
        } else {
          Space stmp(ss.str().c_str());
          Pair* ptmp = pair_->clone(&stmp);
          ptmp->initEnergy();
          stmp.cellOff();
          const double peNew = ptmp->peTot();
          delete ptmp;
          de_ = peNew - pair_->peTot();
          lnpMet_ = space()->nMol()*dlnz_[index] - peNew*dbeta_[index]
                  - criteria_->beta()*de_;
          reject_ = 0;
          if (criteria_->accept(lnpMet_, pair_->peTot() + de_,
                                trialType_.c_str(), reject_) == 1) {
            trialAccept_();
            space()->swapPositions(&stmp);
            if (space()->cellType() > 0) space()->buildCellList();
            if (pair_->neighOn()) pair_->buildNeighList();
            pair_->initEnergy();
            stringstream ss;
            ss << "tmp/swpp" << process_[index] << "p" << proc_ << "o"
               << currentOrder;
            stmp.writeRestart(ss.str().c_str());
          } else {
            trialReject_();
          }
        }
      }
    }
  }
}

/*
 * add nMol which overlaps with a given processor
 */
void TrialConfSwapTXT::addProcOverlap(
  const double order,  //!< order parameter in overlap region
  const int proc,   //!< overlapping processor number
  const double dbeta,   //!< change in beta of overlapping processor
  const double dlnz) {     //!< change in lnz of overlapping processor
  order_.push_back(order);
  process_.push_back(proc);
  nLines_.push_back(0);
  dbeta_.push_back(dbeta);
  dlnz_.push_back(dlnz);
}

}  // namespace feasst

