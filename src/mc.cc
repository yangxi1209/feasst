/*
 * FEASST - Free Energy and Advanced Sampling Simulation Toolkit
 * http://pages.nist.gov/feasst, National Institute of Standards and Technology
 * Harold W. Hatch, harold.hatch@nist.gov
 *
 * Permission to use this data/software is contingent upon your acceptance of
 * the terms of LICENSE.txt and upon your providing
 * appropriate acknowledgments of NIST's creation of the data/software.
 */

#include "./mc.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "./trial_add.h"
#include "./trial_delete.h"
#include "./trial_transform.h"
#include "./analyze_traj.h"

namespace feasst {

MC::MC(Space* space,
       Pair* pair,
       Criteria* criteria)
    : space_(space),
      pair_(pair),
      criteria_(criteria) {
  defaultConstruction_();
  zeroStat();
}
MC::MC(const char* fileName) {
  // cout << "MC restarting" << endl;
  defaultConstruction_();
  ASSERT(fileExists(fileName), "restart file(" << fileName
    << ") doesn't exist");

  // initialize random number generator
  initRNG(fileName);

  // cout << "MC initialize space" << endl;
  string strtmp = fstos("rstFileSpace", fileName);
  ASSERT(!strtmp.empty(), "file name of space restart file not provided");
  space_ = new Space(strtmp.c_str());
  spaceOwned_ = true;

  // cout << "MC initialize pair" << endl;
  strtmp = fstos("rstFilePair", fileName);
  ASSERT(!strtmp.empty(), "file name of pair restart file not provided");
  pair_ = makePair(space_, strtmp.c_str());
  pairOwned_ = true;

  // cout << "initialize criteria" << endl;
  strtmp = fstos("rstFileCriteria", fileName);
  ASSERT(!strtmp.empty(), "file name of criteria restart file not provided");
  criteria_ = makeCriteria(strtmp.c_str());
  criteriaOwned_ = true;

  // cout << "MC initialize trials" << endl;
  strtmp = fstos("nTrials", fileName);
  ASSERT(!strtmp.empty(), "number of trials in restart file not provided");
  const int ntrials = stoi(strtmp);
  for (int i = 0; i < ntrials; ++i) {
    stringstream ss;
    ss << "trialWeight" << i;
    const string trialWeightStr = fstos(ss.str().c_str(), fileName);
    ASSERT(!trialWeightStr.empty(), "trial weight not provided in restart"
           << "file(" << fileName << ")");
    weight = stod(trialWeightStr);
    ss.str("");
    ss << "rstFileTrial" << i;
    const string trialFileStr = fstos(ss.str().c_str(), fileName);
    ASSERT(!trialFileStr.empty(), "trial restart file(" << ss.str()
           << ") not provided in restart file");
    const string trialClassName = fstos("className", trialFileStr.c_str());
    ASSERT(!trialClassName.empty(), "trialClassName not provided in restart"
           << "file(" << trialFileStr << ")");
    #ifdef MPI_H_
      cout << "trialClassName " << trialClassName << " trialFileStr " << trialFileStr << endl;
      ASSERT(trialClassName.compare("TrialConfSwapOMP") != 0,
             "TrialConfSwapOMP requires -D _OPENMP during compilation");
    #endif  // MPI_H_
    #ifdef _OPENMP
      ASSERT(trialClassName.compare("TrialConfSwapTXT") != 0,
             "TrialConfSwapTXT requires -D TXT_H_ during compilation");
    #endif  // _OPENMP
    initTrial(makeTrial(pair_, criteria_, trialFileStr.c_str()));
  }

  strtmp = fstos("nRstFileAnalyze", fileName);
  if (!strtmp.empty()) {
    const int nanalyzer = stoi(strtmp);
    for (int i = 0; i < nanalyzer; ++i) {
      stringstream ss;
      ss << "rstFileAnalyze" << i;
      const string trialAnaStr = fstos(ss.str().c_str(), fileName);
      initAnalyze(makeAnalyze(pair_, trialAnaStr.c_str()));
    }
  }

  nAttempts_ = fstoll("nAttempts", fileName);
  logFileName_ = fstos("logFileName", fileName);
  nFreqLog_ = fstoi("nFreqLog", fileName);
  strtmp = fstos("nFreqXTC", fileName);
  if (!strtmp.empty()) {
    nFreqXTC_ = stoi(strtmp);
    XTCFileName_ = fstos("XTCFileName", fileName);
  }

  nFreqCheckE_ = fstoi("nFreqCheckE", fileName);
  nFreqTune_ = fstoi("nFreqTune", fileName);
  nFreqRestart_ = fstoi("nFreqRestart", fileName);
  checkEtol_ = fstod("checkEtol", fileName);
  strtmp = fstos("production", fileName);
  if (!strtmp.empty()) {
    production_ = stoi(strtmp);
  }

  strtmp = fstos("prodFileAppend", fileName);
  if (!strtmp.empty()) {
    prodFileAppend_ = strtmp;
  }

  // make a different rst file name so as to not overwrite
  // stringstream ss;
  // ss << fileName << "p";
  // rstFileName_ = ss.str();
  rstFileName_ = fileName;
  rstFileBaseName_ = rstFileName_;

  // initialize energy
  pair_->initEnergy();

  // print to log file, but comment out the initial line (to test restarts)
  printLogHeader_ = 2;
  printStat();
}

void MC::defaultConstruction_() {
  className_.assign("MC");
  spaceOwned_ = false;
  pairOwned_ = false;
  criteriaOwned_ = false;
  verbose_ = 0;
  nFreqLog_ = 1e6;
  nFreqXTC_ = 0;
  nFreqCheckE_ = 1e6;
  nFreqTune_ = 0;
  nFreqRestart_ = 1e8;
  printLogHeader_ = 1;
  checkEtol_ = 1e-7;
  nAttempts_ = 0;
  production_ = 0;
  setProductionFileDescription();
}

MC::~MC() {
  // printStat();
  cout << "# MC " << id() << " version " << VERSION << " elapsed time: "
       << double(clock()) / double(CLOCKS_PER_SEC) << " seconds" << endl;
  // cout << "owndership s " << spaceOwned_ << " p " << pairOwned_
  //      << " c " << criteriaOwned_ << endl;
  checkTrialCriteria();
  if (criteriaOwned_) {
    if (className_.compare("MC") == 0) {
      delete criteria_;
    } else if (className_.compare("WLTMMC") == 0) {
      // do nothing as WLTMMC will handle criteria
      //  still, use this to catch exceptions
    } else {
      ASSERT(0, "unrecognized className(" << className_ << ") while cloning");
    }
  }
  if (pairOwned_) delete pair_;
  if (spaceOwned_) delete space_;
}

void MC::reconstruct() {
  Space* space = space_->clone();
  spaceOwned_ = true;
  Pair* pair = pair_->clone(space);
  pairOwned_ = true;
  Criteria* criteria = criteria_;
  if (className_.compare("MC") == 0) {
    criteria = criteria_->clone();
    criteriaOwned_ = true;
    criteria_ = criteria;
  } else if (className_.compare("WLTMMC") == 0) {
    // do nothing as WLTMMC will handle criteria
    //  still, use this to catch exceptions
  } else {
    ASSERT(0, "unrecognized className(" << className_ << ") while cloning");
  }
  // swap temporaries for exception safety
  space_ = space;
  pair_ = pair;
  // clone and reconstruct all trials, while preserving order of trial types
  int nConfSwap = 0;
  for (unsigned int t = 0; t < trialVec_.size(); ++t) {
    if (trialVec_[t]->className() == "TrialConfSwapOMP") {
      #ifdef _OPENMP
        shared_ptr<TrialConfSwapOMP> trial = trialConfSwapVec_[nConfSwap]->cloneShrPtr(pair, criteria);
        trialConfSwapVec_[nConfSwap] = trial;
        trialVec_[t] = trial;
      #endif  // _OPENMP
      ++nConfSwap;
    } else if (trialVec_[t]->className() == "TrialConfSwapTXT") {
      #ifdef MPI_H_
        shared_ptr<TrialConfSwapTXT> trial = trialConfSwapVec_[nConfSwap]->cloneShrPtr(pair, criteria);
        trialConfSwapVec_[nConfSwap] = trial;
        trialVec_[t] = trial;
      #endif  // MPI_H_
      ++nConfSwap;
    } else {
      shared_ptr<Trial> trial = trialVec_[t]->cloneShrPtr(pair, criteria);
      trialVec_[t] = trial;
    }
  }

  // clone and reconstruct all analyzers
  for (unsigned int ia = 0; ia < analyzeVec_.size(); ++ia) {
    shared_ptr<Analyze> an = analyzeVec_[ia]->cloneShrPtr(pair);
    analyzeVec_[ia] = an;
  }

  zeroStat();

  Base::reconstruct();
}

MC* MC::clone() const {
  MC* mc = new MC(*this);
  mc->reconstruct();
  return mc;
}

shared_ptr<MC> MC::cloneImpl() const {
  shared_ptr<MC> mc = make_shared<MC>(*this);
  mc->reconstruct();
  return mc;
}
shared_ptr<MC> MC::cloneShallowImpl() const {
  shared_ptr<MC> mc = make_shared<MC>(*this);
  mc->removeOwnership();
  return mc;
}

void MC::initTrial(shared_ptr<Trial> trial) {
  trial->reconstruct(pair_, criteria_);
  trialVec_.push_back(trial);
  trialWeight_.push_back(weight);

  // update cumulative probability of trials
  updateCumulativeProb_();
}

void MC::removeTrial(int iTrial) {
  if (iTrial == -1) iTrial = nTrials() - 1;
  ASSERT(iTrial < nTrials(), "iTrial(" << iTrial << ") is too big,"
    << "trial doesn't exist.")
  trialVec_.erase(trialVec_.begin() + iTrial);
  trialWeight_.erase(trialWeight_.begin() + iTrial);
  updateCumulativeProb_();
}

void MC::confSwapTrial() {
  #ifdef MPI_H_
    trialConfSwapVec_.push_back(make_shared<TrialConfSwapTXT>
      (pair_, criteria_));
    initTrial(trialConfSwapVec_.back());
  #endif  // MPI_H_
  #ifdef _OPENMP
    trialConfSwapVec_.push_back(make_shared<TrialConfSwapOMP>
      (pair_, criteria_));
    initTrial(trialConfSwapVec_.back());
  #endif  // _OPENMP
}

void MC::attemptTrial() {
  // catch errors and potential problems on first attempt
  if (nAttempts_ == 0) {
    pair_->initEnergy();
    ASSERT(trialVec_.size() != 0, "no trial moves defined");
  }

  const int itrial = ranFromCPDF(trialCumulativeProb_);
  trialVec_[itrial]->attempt();
  peAccumulator_.accumulate(pair_->peTot());
  nMolAccumulator_.accumulate(space_->nMol());
  ++nAttempts_;

  afterAttempt_();
}

void MC::zeroStat() {
  criteria_->zeroStat();
  for (unsigned int i = 0; i < trialVec_.size(); ++i) {
    (*trialVec_[i]).zeroStat();
  }
  peAccumulator_.reset();
  nMolAccumulator_.reset();
  nAttempts_ = 0;
}

void MC::printStat() {
  // print to log file
  if (!logFileName_.empty()) {
    space_->storeUniqueConfigID();
    std::ofstream log_(logFileName_.c_str(),
                       std::ofstream::out | std::ofstream::app);
    if (printLogHeader_ > 0) {
      log_ << "#attempts pe/nMol ";
      for (unsigned int i = 0; i < trialVec_.size(); ++i) {
        log_ << trialVec_[i]->printStat(true);
      }
      log_ << "configID" << endl;
      if (printLogHeader_ == 2) {
        printLogHeader_ = -1;
      } else {
        printLogHeader_ = 0;
      }
    }
    if (printLogHeader_ == -1) {
      log_ << "# ";
      printLogHeader_ = 0;
    }
    log_ << nAttempts_ << " " << pePerMol() << " ";
    for (unsigned int k = 0; k < trialVec_.size(); ++k) {
      log_ << trialVec_[k]->printStat(false);
    }
    log_ << space_->configID() << endl;
  }
}

double MC::pePerMol() {
  double pe = 0;
  if (space_->nMol() != 0) pe = pair_->peTot()/space_->nMol();
  return pe;
}

/*
 *  In this version, clone mc and swap criteria for increase or decrease of
 *  the activity
 */
void MC::nMolSeek(
  const int nTarget,          //!< number of molcules to seek
  const char* molType,        //!< type of molecule
  long long maxAttempts,   //!< maximum attempts before failure
  const double volumeExpansion
  ) {
  std::string molTypeStr(molType);
  if (nTarget != space_->nMol()) {
    ASSERT(nTarget >= 0, "nMolSeek n(" << nTarget << ") < 0");

    // progress report
    std::ofstream log_(logFileName_.c_str(),
                       std::ofstream::out | std::ofstream::app);
    const int nStart = space_->nMol();
    const double nchange = std::abs(nTarget - nStart);
    const double npercent = 0.25;
    double ncurrentper = npercent;

    // make a "shallow" clone of the monte carlo simulation
    //  e.g., clone does not own space,pair,criteria,trials
    shared_ptr<MC> mc = cloneShallowShrPtr();
    // shared_ptr<MC> mc = (this)->cloneShallowShrPtr();

    // modify trials to improve efficiency
    // first, add an "add" or "delete" trial
    // automatically select trial weight so new trial occurs >25%
    const double wtTot = std::accumulate(trialWeight_.begin(),
                                         trialWeight_.end(), 0.);
    mc->weight = wtTot/4.;
    if (fabs(wtTot) < DTOL) mc->weight = 1.;  // if no other trials
    if (nTarget > space_->nMol()) {
      if (molTypeStr.empty()) {
        addTrial(mc, space_->addMolListType()[0].c_str());
      } else {
        addTrial(mc, molType);
      }
    // otherwise, if need less particles, use a small chemical potential
    } else {
      deleteTrial(mc);
    }

    // Tell all trials that there is a target number of particles to obtain,
    // and that they may break detailed balance to speed up the process.
    for (int iTrial = 0; iTrial < nTrials(); ++iTrial) {
      trialVec_[iTrial]->initializeNMolSeek(nTarget);
    }

    // next, remove trials which don't help or break
    for (int iTrial = mc->nTrials() - 1; iTrial >= 0; --iTrial) {
      if (mc->trialVec()[iTrial]->className() == "TrialAdd") {
        if (nTarget < space_->nMol()) {
          mc->removeTrial(iTrial);
        }
      } else if (mc->trialVec()[iTrial]->className() == "TrialDelete") {
        if (nTarget > space_->nMol()) {
          mc->removeTrial(iTrial);
        }
      } else if (mc->trialVec()[iTrial]->className() == "TrialBeta") {
        mc->removeTrial(iTrial);
      }
    }

    // replace acceptance criteria (restore once nTarget reached)
    // make acceptance criteria more ammenable to nMol changes
    int nMax = space_->nMol();
    if (nMax < nTarget) nMax = nTarget;
    CriteriaWLTMMC cnew(criteria_->beta()/4., criteria_->activ(0), "nmol",
                        -0.5, nMax + 0.5, nMax + 1);
    for (int ia = 1; ia < criteria_->nActiv(); ++ia) {
      cnew.addActivity(criteria_->activ(ia));
    }

    // If pressure is set in criteria and trying to increase particles,
    // expand volume by volumeExpansion
    const double originalVolume = space_->volume();
    const double originalNMol = space_->nMol();
    if ((criteria_->pressureFlag() == 1) && (nTarget > space_->nMol())) {
      cnew.pressureset(1e7);
      space_->scaleDomain(volumeExpansion);
    }
    mc->replaceCriteria(&cnew);

    // iterate maxAttempt trials until target n is reached, or error
    int iAttempt = 0;
    const int printLogHeaderOrig = mc->printLogHeader_;
    while ( (nTarget != space_->nMol()) && (iAttempt < maxAttempts) ) {
      mc->printLogHeader_ = -1;  // comment out log file prints while seeking
      mc->attemptTrial();
      ++iAttempt;

      // output progress report
      if (ncurrentper < std::abs(nStart - space_->nMol())/nchange) {
        log_ << "# nMolSeek is more than " << ncurrentper*100
             << " percent done at n=" << space_->nMol() << " of "
             << nTarget << " at attempt " << iAttempt << " out of "
             << maxAttempts << endl;
        ncurrentper += npercent;
      }
    }
    ASSERT(nTarget == space_->nMol(),
      "nMolSeek did not reach the desired number of moles (" << nTarget
      << ") within the number of maxAttempts (" << maxAttempts << ")");

    // If pressure was set in criteria and trying to increase particles,
    // now attempt to apply pressure with volume moves to squeeze box back.
    if ((criteria_->pressureFlag() == 1) && (nTarget > originalNMol)) {
      mc->weight /= space_->nMol()/4.;
      transformTrial(mc.get(), "vol", 0.01);
      log_ << "# attempting to squeeze the box back to original size" << endl;
      while ((space_->volume() > originalVolume) && (iAttempt < maxAttempts)) {
        mc->printLogHeader_ = -1;  // comment out log file prints while seeking
        mc->attemptTrial();
        ++iAttempt;
      }
      ASSERT(space_->volume() < originalVolume,
        "nMolSeek could not compress the box of current size("
        << space_->volume() << ") back to original size(" << originalVolume
        << ") within the number of maxAttempts (" << maxAttempts << ")");
      /// scale volume to exactly the same as before
      space_->scaleDomain(originalVolume/space_->volume());
      pair_->initEnergy();
      ASSERT(fabs((space_->volume() - originalVolume)/originalVolume) < DTOL,
        "volume(" << space_->volume() << ") is not same as before nMolSeek("
        << originalVolume << "). Difference: "
        << space_->volume() - originalVolume);
      // remove the last trial, which should be volume move
      mc->removeTrial();
    }

    // Print final status to log
    log_ << "# nMolSeek done at attempt " << iAttempt << " out of "
         << maxAttempts << endl;
    printLogHeader_ = printLogHeaderOrig;  // restore print log header state

    // restore criteria in all trials
    mc->restoreCriteria();

    // Tell all trials that there is no longer a target
    for (int iTrial = 0; iTrial < nTrials(); ++iTrial) {
      trialVec_[iTrial]->initializeNMolSeek();
    }
  }
}

void MC::neighAVBInit(const double rAbove,    //!< upper bound
                      const double rBelow     //!< lower bound
  ) {
  if (pair_->neighOn() == false) {
    pair_->initNeighList(rAbove, rBelow);
    pair_->buildNeighList();
  } else {
    ASSERT((pair_->neighAbove() == rAbove) && (pair_->neighBelow() == rBelow),
      "avb trial move added when neighbor list already exists with different"
      << "cutoff. Current neighbor list (" << pair_->neighBelow() << ", " <<
      pair_->neighAbove() << "). New AVB (" << rBelow << ", " << rAbove << ")");
  }
}

void MC::afterAttemptBase_() {
  // write restart file
  if (nAttempts_ % nFreqRestart_ == 0) {
    writeRestart(rstFileName_.c_str());
  }

  // check energy, cell list and neigh list
  if (nFreqCheckE_ > 0) {
    if (nAttempts_ % nFreqCheckE_ == 0) {
      if (space_->cellType() > 0) {
        space_->checkCellList();
      }
      pair_->checkEnergy(checkEtol_, 0);
      if (pair_->neighOn()) pair_->checkNeigh();
    }
  }

  // print stats
  if (nAttempts_ % nFreqLog_ == 0) {
    printStat();
    if (space_->clusterType().size() != 0) {
      std::stringstream ss;
      ss << logFileName_ << "clus";
      space_->printClusterStat(ss.str().c_str());
    }
  }

  // print xyz would go ~here in terms of order.
  // When these are all converted to Analyze, need to check orders.

  // print xtc
  #ifdef XDRFILE_H_
  if ( (nFreqXTC_ != 0) && (production_ == 1) ) {
    if (nAttempts_ % nFreqXTC_ == 0) {
      if (!XTCFileName_.empty()) {
        stringstream ss;
        ss << XTCFileName_ << "n" << space_->nMol();
        string mode("a");
        if (nAttempts_ == nFreqXTC_) {
          mode.assign("w");
        }
        pair_->printXYZ(ss.str().c_str(), 2);
        ss << ".xtc";
        XDRFILE* trjFileXDR;
        trjFileXDR = xdrfile_open(ss.str().c_str(), mode.c_str());
        space_->writeXTC(trjFileXDR);
        xdrfile_close(trjFileXDR);
      }
    }
  }
  #endif  // XDRFILE_H_

//  // print p(N,E)
//  if (production_ == 1) {
//    if (nAttempts_ % 1000 == 0) {
//      stringstream ss;
//      ss << logFileName_ << "pne.txt";
//      //std::ofstream log_(ss.str().c_str(),
//                           std::ofstream::out | std::ofstream::app);
//      std::ofstream log_("yoyo", std::ofstream::out | std::ofstream::app);
//      log_ << space_->nMol() << " " << pair_->peTot() << endl;
//    }
//  }

  // tune translation move parameters
  if ( (nFreqTune_ != 0) && (nAttempts_ % nFreqTune_ == 0) ) {
    tuneTrialParameters_();
  }

  // run analyzers if not multiple macrostates (e.g., no WLTMMC)
  if (className_.compare("MC") == 0) {
    for (vector<shared_ptr<Analyze>>::iterator it = analyzeVec_.begin();
         it != analyzeVec_.end(); ++it) {
      if (nAttempts_ % (*it)->nFreq() == 0) {
        (*it)->update();
      }
      if (nAttempts_ % (*it)->nFreqPrint() == 0) {
        (*it)->write();
      }
    }
  }
}

int MC::nMolMax(
  const long long npr,   // number of steps in production run
  const double activ,       // large activity
  const int nMolExtra       // add extra mols
  ) {
  const double activOld = criteria_->activ();
  criteria_->activset(activ);
  runNumTrials(npr);
  zeroStat();
  criteria_->activset(activOld);
  const int nmx = space_->nMol() + nMolExtra;
  std::ofstream log_(logFileName_.c_str(),
                     std::ofstream::out | std::ofstream::app);
  log_ << "# " << className_ << " found maximum number of mols: " << nmx
       << endl;
  return nmx;
}

void MC::run() {
  for (long long i = 0; i < npr_; ++i) {
    attemptTrial();
  }
}

int MC::checkTrialCriteria() {
  int match = 1;
  if (trialVec_.size() > 0) {
    if (trialVec_.front()->criteria() != criteria_) {
      match = 0;
    }
  }
  if (trialVec_.size() > 1) {
    for (unsigned int t = 1; t < trialVec_.size(); ++t) {
      if (trialVec_.front()->criteria() != trialVec_[t]->criteria()) {
        match = 0;
      }
    }
  }
  ASSERT(match, "not all criteria in trials match");
  return match;
}

void MC::writeRestart(const char* fileName) {
  fileBackUp(fileName);
  std::ofstream file(fileName);
  file << "# className " << className_ << endl;

  stringstream ss;
  ss << fileName << "space";
  space_->writeRestart(ss.str().c_str());
  file << "# rstFileSpace " << ss.str() << endl;

  ss.str("");
  ss << fileName << "pair";
  pair_->writeRestart(ss.str().c_str());
  file << "# rstFilePair " << ss.str() << endl;

  ss.str("");
  ss << fileName << "criteria";
  criteria_->writeRestart(ss.str().c_str());
  file << "# rstFileCriteria " << ss.str() << endl;
  file << "# nTrials " << trialVec_.size() << endl;
  for (unsigned int i = 0; i < trialVec_.size(); ++i) {
    ss.str("");
    ss << fileName << "trial" << i;
    trialVec_[i]->writeRestart(ss.str().c_str());
    file << "# rstFileTrial" << i << " " << ss.str() << endl;
    file << "# trialWeight" << i << " " << trialWeight_[i] << endl;
  }
  file << "# nAttempts " << nAttempts_ << endl;
  file << "# logFileName " << logFileName_ << endl;
  file << "# nFreqLog " << nFreqLog_ << endl;
  file << "# nFreqXTC " << nFreqXTC_ << endl;
  file << "# XTCFileName " << XTCFileName_ << endl;
  file << "# nFreqCheckE " << nFreqCheckE_ << endl;
  file << "# nFreqTune " << nFreqTune_ << endl;
  file << "# nFreqRestart " << nFreqRestart_ << endl;
  file << "# checkEtol " << checkEtol_ << endl;
  if (production_ == 1) file << "# production " << production_ << endl;
  file << "# prodFileAppend " << prodFileAppend_ << endl;

  // write random number generator state
  writeRngRestart(fileName);

  // write analyzer restarts
  if (analyzeVec_.size() != 0) {
    file << "# nRstFileAnalyze " << analyzeVec_.size() << endl;
  }
  for (unsigned int iAn = 0; iAn < analyzeVec_.size(); ++iAn) {
    ss.str("");
    ss << fileName << "analyze" << iAn;
    file << "# rstFileAnalyze" << iAn << " " << ss.str() << endl;
    analyzeVec_[iAn]->writeRestart(ss.str().c_str());
  }
}

void MC::b2init_() {
  ASSERT(space_->nMol() <= 2, "no more than two molecules may be present" <<
         "before b2 calc, nMol=" << space_->nMol());
  ASSERT(npr_ > nFreqLog_*3, "for b2, npr(" << npr_ << ") must be at least 3"
         << "times greater than nFreqLog(" << nFreqLog_ <<")");

  if (space_->nMol() == 0) {
    // add first molecule on origin
    vector<double> xAdd(space_->dimen());
    pair_->addMol(xAdd);
  }

  if (space_->nMol() == 1) {
    // add second molecule randomly within domain
    pair_->addMol();
  }
}

void MC::b2(const double tol, double &b2v, double &b2er, double boxl) {
  b2init_();

  const vector<int> mpart = space_->imol2mpart(1);

  // initialize domain
  if (boxl == -1) {
    boxl = 2.*(2.*space_->maxMolDist() + pair_->rCut());
  }

  // cout << "boxl " << boxl << " maxmold " << space_->maxMolDist()
  //      << " rc " << pair_->rCut() << endl;

  // const double boxlbig = 1e6;
  const double boxlbig = boxl*1e6;
  const double vol = pow(boxl, space_->dimen());
  vector<double> dr;
  for (int dim = 0; dim < space_->dimen(); ++dim) dr.push_back(0.5*boxl);

  // monte carlo integration
  Accumulator meyer, m2, mo;
  for (long long itrial = 0; itrial < npr_; ++itrial) {
    // move and rotate second molecule randomly within domain
    for (int dim=0; dim < space_->dimen(); ++dim) space_->initBoxLength(boxl, dim);
    space_->randDisp(mpart, 0.5*boxl);
    space_->randRotate(mpart, -1);

    // expand domain so there are no mirror images
    for (int dim=0; dim < space_->dimen(); ++dim) space_->initBoxLength(boxlbig, dim);

    // compute energy and meyer function
    pair_->initEnergy();
    const double pe = pair_->peTot();
    // const double pe = pair_->multiPartEner(mpart, 0);

    meyer.accumulate(-0.5*vol*(exp(-criteria_->beta()*pe) - 1));
    mo.accumulate(-0.5*vol*(-pe)*exp(-criteria_->beta()*pe));
    // cout << "pe " << pe << " meyer " << 1 - exp(-criteria_->beta()*pe)
    //      << " acc " << -0.5*vol*(exp(-criteria_->beta()*pe) - 1) << " v "
    //      << vol << endl;

//    if ( (itrial != 0) && (itrial % nFreqMovie_ == 0) ) {
//      int flag = 0;
//      if (itrial == nFreqMovie_) flag = 1;
//      if (!movieFileName_.empty()) {
//        pair_->printXYZ(movieFileName_.c_str(), flag);
//      }
//    }

    if ( (itrial != 0) && (itrial % nFreqLog_ == 0) ) {
      m2.accumulate(meyer.average());
      if (m2.nValues() > 2) {
        b2er = m2.std()/sqrt(m2.nValues());
      } else {
        b2er = 1e200;
      }
      if (!logFileName_.empty()) {
        std::ofstream log_(logFileName_.c_str(),
                           std::ofstream::out | std::ofstream::app);
        log_ << itrial << " " << m2.average() << " " << b2er << " "
             << meyer.average() << " " << mo.average() << endl;
      }
      meyer.reset();
      if ( (itrial != nFreqLog_) && ( (b2er < tol) ||
           (b2er/fabs(m2.average()) < tol) ) ) itrial = npr_;
    }
  }
  b2v = m2.average();
}

void MC::b2mayer(double *b2v, double *b2er, Pair *pairRef, const double tol, double boxl) {
  b2init_();
  ASSERT(space() == pairRef->space(), "reference potential must point to the"
    << "same space object");
  const vector<int> mpart = space_->imol2mpart(1);

  // initialize domain
  if (boxl == -1) {
    boxl = 2.*(2.*space_->maxMolDist() + pair_->rCut());
  }
//  space.initBoxLength(boxl);
//  const double boxlbig = boxl*1e6;
//
//  // equilibrate: tune maxMove parameters and make sure that initial
//  // configuration has non-zero energy
//  double peOld = 0.;
//  const int maxIterations = 1e3;
//  int iter = 0;
//  pair_->initEnergy();
//  while ( (peOld != 0.) && (iter < maxIterations) ) {
//    runNumTrials(1e5);
//    peOld = pair_->peTot();
//    ++iter;
//  }
//  ASSERT(iter < maxIterations, "max iterations reached in equilibration");
//
//  pairRef.initEnergy();
//  double f12old = exp(-peOld/temp) - 1,
//         f12ref = exp(-pairRef.peTot()/temp) - 1;
//
//  // begin trial moves and averaging
//  const double maxDisp = 0.1;
//  Accumulator mayer, mayerRef;
//  for (long long int itrial = 0; itrial < npr_; ++itrial) {
//    // store old position
//    space.xStore(mpart);
//
//    // move and rotate second molecule randomly within domain
//    space_->randDisp(mpart, 0.5*boxl);
//    space_->randRotate(mpart, -1);
//
//    // expand domain so there are no mirror images
//    for (int dim=0; dim < space_->dimen(); ++dim) space_->initBoxLength(boxlbig, dim);
//
//    // compute energy and meyer function
//    pair_->initEnergy();
//    const double pe = pair_->peTot();
//    // const double pe = pair_->multiPartEner(mpart, 0);
//
//    meyer.accumulate(-0.5*vol*(exp(-criteria_->beta()*pe) - 1));
//    // cout << "pe " << pe << " meyer " << 1 - exp(-criteria_->beta()*pe)
//    //      << " acc " << -0.5*vol*(exp(-criteria_->beta()*pe) - 1) << " v "
//    //      << vol << endl;
//
//    if ( (itrial != 0) && (itrial % nFreqMovie_ == 0) ) {
//      int flag = 0;
//      if (itrial == nFreqMovie_) flag = 1;
//      if (!movieFileName_.empty()) {
//        pair_->printXYZ(movieFileName_.c_str(), flag);
//      }
//    }
//
//    if ( (itrial != 0) && (itrial % nFreqLog_ == 0) ) {
//      m2.accumulate(meyer.average());
//      if (m2.nValues() > 2) {
//        b2er = m2.std()/sqrt(m2.nValues());
//      } else {
//        b2er = 1e200;
//      }
//      if (!logFileName_.empty()) {
//        std::ofstream log_(logFileName_.c_str(),
//                           std::ofstream::out | std::ofstream::app);
//        log_ << itrial << " " << m2.average() << " " << b2er << " "
//             << meyer.average() << endl;
//      }
//      meyer.reset();
//      if ( (itrial != nFreqLog_) && ( (b2er < tol) ||
//           (b2er/fabs(m2.average()) < tol) ) ) itrial = npr_;
//    }
//  }
//  b2v = m2.average();
}

double MC::boylemin_(const double beta) {
  criteria_->betaset(beta);
  // cout << "#trying beta " << beta << endl;
  if (beta <= 0) {
    if (!logFileName_.empty()) {
      std::ofstream log_(logFileName_.c_str(),
                         std::ofstream::out | std::ofstream::app);
      log_  << "beta " << beta << " b2 " << 1e20 - beta << endl;
    }
    return 1e20 - beta;
  } else {
    double b2val, b2er = -1;
    b2(boyletol_, b2val, b2er);
    if (!logFileName_.empty()) {
      std::ofstream log_(logFileName_.c_str(),
                         std::ofstream::out | std::ofstream::app);
      log_  << "beta " << beta << " b2 " << b2val << endl;
    }
    if (fabs(b2val) < b2er) {
      return std::numeric_limits<double>::min();
    } else {
      return pow(b2val+sign(b2er, b2val), 2.);
    }
  }
}

// HWH mins
//double MC::boyle(const double tol) {
//  boyletol_ = tol;
//  Golden g;
//  boyleminwrapper_ boylewrap = boyleminwrap_();
//  g.bracket(criteria_->beta(), criteria_->beta()*100, boylewrap);
//  return g.minimize(boylewrap);
//}

/*
 *  first, remove config bias from trialVec, then from trialWeight and trialCumulativeProb
 * */
void MC::removeConfigBias() {
  // update points to trials and trial weights for selection
  const int nTrials = static_cast<int>(trialVec_.size());
  for (int i = nTrials - 1; i >= 0; --i) {
    if (trialVec_[i]->className().compare("TrialConfigBias") == 0) {
      trialVec_.erase(trialVec_.begin() + i);
      trialWeight_.erase(trialWeight_.begin() + i);
    }
  }

  // update cumulative probabilities of trial selection
  updateCumulativeProb_();
}

void MC::updateCumulativeProb_() {
  trialCumulativeProb_.clear();
  const double wtTot = std::accumulate(trialWeight_.begin(),
                                       trialWeight_.end(), 0.);
  double prob = 0;
  for (unsigned int i = 0; i < trialWeight_.size(); ++i) {
    prob += trialWeight_[i]/wtTot;
    trialCumulativeProb_.push_back(prob);
  }
}

void MC::replaceCriteria(Criteria *criteria) {
  criteriaOld_ = criteria_;
  criteria_ = criteria;

  // replace criteria in all trials
  for (unsigned int iTrial = 0; iTrial < trialVec_.size(); ++iTrial) {
    trialVec_[iTrial]->replaceCriteria(criteria);
  }
}


void MC::restoreCriteria() {
  ASSERT(criteriaOld_ != NULL,
    "attempting to restore criteria, but no old criteria recorded");
  criteria_ = criteriaOld_;
  criteriaOld_ = NULL;

  // restore criteria in all trials
  for (unsigned int iTrial = 0; iTrial < trialVec_.size(); ++iTrial) {
    trialVec_[iTrial]->restoreCriteria();
  }
}

void MC::appendFileNames(const char* chars) {
  if (!rstFileName_.empty()) rstFileName_.append(chars);
  MC::appendProductionFileNames(chars);
}
void MC::appendProductionFileNames(const char* chars) {
  if (!XTCFileName_.empty()) XTCFileName_.append(chars);
  if (!logFileName_.empty()) logFileName_.append(chars);
  for (unsigned int ia = 0; ia < analyzeVec_.size(); ++ia) {
    analyzeVec_[ia]->appendFileName(chars);
  }
}

void MC::initProduction() {
  production_ = 1;
  appendProductionFileNames(prodFileAppend_.c_str());
  space_->clusterReset();

  // tell analyzers that production has begun
  for (vector<shared_ptr<Analyze> >::iterator it = analyzeVec_.begin();
       it != analyzeVec_.end();
       ++it) {
    (*it)->initProduction(1);
  }
}

void MC::tuneTrialParameters_() {
  for (vector<shared_ptr<Trial> >::iterator it = trialVec_.begin();
       it != trialVec_.end(); ++it) {
    (*it)->tuneParameters();
  }
}

void MC::initMovie(const char* fileName, const int nfreq) {
  auto analyze = makeAnalyzeTRAJ(pair_,
    {{"nFreqPrint", str(nfreq)},
     {"fileName", fileName},
     {"format", "xyz"}});
  initAnalyze(analyze);
}

}  // namespace feasst
