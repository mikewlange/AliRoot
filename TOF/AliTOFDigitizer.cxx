/**************************************************************************
 * Copyright(c) 1998-2000, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/

//_________________________________________________________________________
// This is a TTask that makes TOF-Digits out of TOF-SDigits. 
// The simulation of the detector is performed at sdigits level:
// during digitization the unique task is the sum of all sdigits in the
// same pad.
// Digits are written to TreeD in branch "TOF".
//
// -- Author :  F. Pierella (Bologna University) pierella@bo.infn.it
//////////////////////////////////////////////////////////////////////////////

#include <TTree.h> 
#include <TVector.h>
#include <TObjArray.h>
#include <TFile.h>
#include <TDirectory.h>
#include <TRandom.h>


#include "AliTOFDigitizer.h"
#include "AliTOF.h"
#include "AliTOFSDigitizer.h"
#include "AliTOFhit.h"
#include "AliTOFdigit.h"
#include "AliTOFSDigit.h"
#include "AliTOFHitMap.h"
#include "AliDigitizer.h"
#include "AliRunDigitizer.h"

#include "AliRun.h"
#include "AliPDG.h"

#include <stdlib.h>
#include <iostream.h>
#include <fstream.h>

ClassImp(AliTOFDigitizer)

//___________________________________________
  AliTOFDigitizer::AliTOFDigitizer()  :AliDigitizer()
{
  // Default ctor - don't use it
  fDigits=0;
  fSDigitsArray=0;
  fhitMap=0;
}

//___________________________________________
AliTOFDigitizer::AliTOFDigitizer(AliRunDigitizer* manager) 
    :AliDigitizer(manager) 
{
  fDigits=0;
  fSDigitsArray=0;
  fhitMap=0;
}

//------------------------------------------------------------------------
AliTOFDigitizer::~AliTOFDigitizer()
{
  // Destructor
}

//---------------------------------------------------------------------

void AliTOFDigitizer::Exec(Option_t* option)
{
  //
  // Perform digitization and merging.
  // The algorithm is the following:
  // - a hitmap is created to check if a pad is already activated;
  // - an sdigits container is created to collect all sdigits from
  //   different files;
  // - sdigits are summed using the hitmap;
  // - the sdigits container is used to create the array of AliTOFdigit.
  //

  if(strstr(option,"deb")) cout<<"AliTOFDigitizer::Exec\n";


  // get the ptr to TOF detector
  AliTOF * tof = (AliTOF *) gAlice->GetDetector("TOF") ;

  //Make branches
  char branchname[20];
  sprintf (branchname, "%s", tof->GetName ());

  fDigits=new TClonesArray("AliTOFdigit",4000);

  TTree* treeD = fManager->GetTreeD();
  //Make branch for digits (to be created in Init())
  tof->MakeBranchInTree(treeD,branchname,&fDigits,4000);

  // container for all summed sdigits (to be created in Init())
  fSDigitsArray=new TClonesArray("AliTOFSDigit",1000);
  
  // create hit map (to be created in Init())
  fhitMap = new AliTOFHitMap(fSDigitsArray);
  
  // Loop over files to digitize

  for (Int_t inputFile=0; inputFile<fManager->GetNinputs();
       inputFile++) {
    ReadSDigit(inputFile);
  }

  // create digits
  CreateDigits();

  // free used memory for Hit Map in current event
  delete fhitMap;
  fSDigitsArray->Delete();
  treeD->Fill();
 
  fManager->GetTreeD()->AutoSave(); // to fit with the framework
  fDigits->Delete();
  delete fDigits;

}

//---------------------------------------------------------------------

void AliTOFDigitizer::CreateDigits()
{
  // loop on sdigits container to fill the AliTOFdigit TClonesArray
  // start digitizing all the collected sdigits 

  Int_t ndump=15; // dump the first ndump created digits for each event

  // get the total number of collected sdigits
  Int_t ndig = fSDigitsArray->GetEntriesFast();

  for (Int_t k = 0; k < ndig; k++) {
    
    Int_t    vol[5];       // location for a digit
    
    // Get the information for this digit
    AliTOFSDigit *tofsdigit = (AliTOFSDigit *) fSDigitsArray->UncheckedAt(k);
    
    Int_t nslot=tofsdigit->GetNDigits(); // get the number of slots
    // for current sdigit
    
    // TOF sdigit volumes (always the same for all slots)
    Int_t sector    = tofsdigit->GetSector(); // range [1-18]
    Int_t plate     = tofsdigit->GetPlate();  // range [1- 5]
    Int_t strip     = tofsdigit->GetStrip();  // range [1-20]
    Int_t padz      = tofsdigit->GetPadz();   // range [1- 2]
    Int_t padx      = tofsdigit->GetPadx();   // range [1-48]
    
    vol[0] = sector;
    vol[1] = plate;
    vol[2] = strip;
    vol[3] = padx;
    vol[4] = padz;
    
    //--------------------- QA section ----------------------
    // in the while, I perform QA
    Bool_t isSDigitBad = (sector<1 || sector>18 || plate<1 || plate >5 || padz<1 || padz>2 || padx<1 || padx>48);
    
    if (isSDigitBad) {
      cout << "<AliTOFSDigits2Digits>  strange sdigit found" << endl;
      abort();
    }
    //-------------------------------------------------------
    
    //------------------- Dump section ----------------------
    if(k<ndump){
      cout << k << "-th | " << "Sector " << sector << " | Plate " << plate << " | Strip " << strip << " | PadZ " << padz << " | PadX " << padx << endl;
      cout << k << "-th sdigit" << endl;
      cout << "----------------------------------------------------"<< endl;
    }
    // ------------------------------------------------------
    
    // start loop on number of slots for current sdigit
    for (Int_t islot = 0; islot < nslot; islot++) {
      Float_t  digit[2];     // TOF digit variables
      Int_t tracknum[kMAXDIGITS];     // contributing tracks for the current slot
      
      Float_t tdc=tofsdigit->GetTdc(islot); digit[0]=tdc;
      Float_t adc=tofsdigit->GetAdc(islot); digit[1]=adc;
      
      tracknum[0]=tofsdigit->GetTrack(islot,0);
      tracknum[1]=tofsdigit->GetTrack(islot,1);
      tracknum[2]=tofsdigit->GetTrack(islot,2);
      
      
      // adding a TOF digit for each slot
      AliTOFdigit* newDigit=new AliTOFdigit(tracknum, vol, digit);
      fDigits->Add(newDigit);
    }
    
  } // end loop on sdigits - end digitizing all collected sdigits

}

//---------------------------------------------------------------------

void AliTOFDigitizer::ReadSDigit(Int_t inputFile )
{
  // Read sdigits for current event and inputFile; 
  // store them into the sdigits container
  // and update the hit map
  // SDigits from different files are assumed to
  // be created with the same simulation parameters.
  
  // get the treeS from manager
  TTree* currentTreeS=fManager->GetInputTreeS(inputFile);
  
  // get the branch TOF inside the treeS
  TClonesArray * sdigitsDummyContainer= new TClonesArray("AliTOFSDigit",  1000); 

  // check if the branch exist
  TBranch* tofBranch=currentTreeS->GetBranch("TOF");

  if(!tofBranch){
    Fatal("ReadSDigit","TOF branch not found for input %d",inputFile);
  }
  
  tofBranch->SetAddress(&sdigitsDummyContainer);           
  
  Int_t nEntries = (Int_t)tofBranch->GetEntries();                                

  // Loop through all entries in the tree
  Int_t nbytes;
  
  for (Int_t iEntry = 0; iEntry < nEntries; iEntry++) {
    
    // Import the tree
    nbytes += tofBranch->GetEvent(iEntry);
    
    // Get the number of sdigits
    Int_t ndig = sdigitsDummyContainer->GetEntriesFast();
    
    for (Int_t k=0; k<ndig; k++) {
      AliTOFSDigit *tofSdigit= (AliTOFSDigit*) sdigitsDummyContainer->UncheckedAt(k);
      
      Int_t    vol[5];       // location for a sdigit
      
      // check the sdigit volume
      vol[0] = tofSdigit->GetSector();
      vol[1] = tofSdigit->GetPlate();
      vol[2] = tofSdigit->GetStrip();
      vol[3] = tofSdigit->GetPadx();
      vol[4] = tofSdigit->GetPadz();
      
      if (fhitMap->TestHit(vol) != kEmpty) {
	AliTOFSDigit *sdig = static_cast<AliTOFSDigit*>(fhitMap->GetHit(vol));
	sdig->Update(tofSdigit);

      } else {

	CollectSDigit(tofSdigit); // collect the current sdigit
	fhitMap->SetHit(vol);     // update the hitmap for location vol

      } // if (hitMap->TestHit(vol) != kEmpty)
      
    } // for (Int_t k=0; k<ndig; k++)

  } // end loop on entries

  sdigitsDummyContainer->Delete();
  sdigitsDummyContainer=0;

}


//_____________________________________________________________________________
void AliTOFDigitizer::CollectSDigit(AliTOFSDigit * sdigit)
{
  //
  // Add a TOF digit
  // new with placement used
  AliTOFSDigit sdigitCopy=AliTOFSDigit(*sdigit); // make a copy of the current sdigit
  fSDigitsArray->Add(&sdigitCopy); // put it into tmp array
}
