#include "EDA_Correction.h"


//EdaCorrection::EdaCorrection(EdaCorrection::EmotiBitVersion version, EdaCorrection::OtpDataFormat dataFormat)
//{
//	emotiBitVersion = version;
//	otpDataFormat = dataFormat;
//}

EdaCorrection::Status EdaCorrection::enterUpdateMode(EdaCorrection::EmotiBitVersion version, EdaCorrection::OtpDataFormat dataFormat)
{
	
	Serial.println("Enabling Mode::UPDATE");
	_mode = EdaCorrection::Mode::UPDATE;
	emotiBitVersion = version;
	otpDataFormat = dataFormat;
	Serial.print("\n**Enter Dummy mode?**\n"); Serial.println("\tPress Y to work in dummy mode, N to actually write to OTP");
	while (!Serial.available());
	if (Serial.read() == 'Y')
	{
		dummyWrite = true;
		Serial.println("###################");
		Serial.println("## IN DUMMY MODE ##");
		Serial.println("###################");
	}
	else
	{
		Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
		Serial.println("!!!! Actually writing to the OTP  !!!!");
		Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
	}
	Serial.println("Initializing state machine. State: WAITING_FOR_SERIAL_DATA");
	progress = EdaCorrection::Progress::WAITING_FOR_SERIAL_DATA;
	Serial.println("Once you have the appropriate data, please plug in the serial cable and enter the data.");
	Serial.print("The input should be in the format:");
	Serial.println("EDL_0R, EDL_10K, EDL_100K, EDL_1M, EDL_10M, vRef2_0R, vRef2_10K, vRef2_100K, vRef2_1M, vRef2_10M");
	Serial.print("Proceeding with normal execution in\n");
	uint8_t msgTimer = 5;
	while (msgTimer > 0)
	{
		Serial.print(msgTimer); Serial.print("\t");
		delay(1000);
		msgTimer--;
	}
	return EdaCorrection::Status::SUCCESS;

}

void EdaCorrection::normalModeOperations(float &vref1, float &vref2, float &Rfeedback)
{
	// reading the OTP is done in the ISR
	// the correction values should be generated in the update function outside the ISR,
	// to reduce the load on the ISR
	// call calcCorrection
	if (readOtpValues && !calculationPerformed)
	{
		if (isOtpValid)// check if the metadata byte is present
		{
			calcEdaCorrection();
		}
		else
		{
			Serial.println("OTP has not been updated. Not performing correction calculation.");
			Serial.println("Perform EDA correction first.");
			Serial.println("Using EDA with correction");
			//edaCorrection.calculationPerformed = true;
			progress = EdaCorrection::Progress::FINISH;
		}
	}

	// register overwrite occurs in UPDATE mode, but once it occurs, the mode is shifted to NORMAL and is detected here 
	if (triedRegOverwrite)
	{
		Serial.println("You are trying to overwrite a register, which is not allowed.");
		Serial.println("Please verify write operations");
		//edaCorrection.triedRegOverwrite = false;// out of UPDATE mode, so this will not affect write Operations
		progress = EdaCorrection::Progress::FINISH;// prevents re-enterin normal mode operations
	}

	if (correctionDataReady)
	{
		Serial.print("Estimated Rskin values BEFORE correction:|");
		float RskinEst = 0;
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			RskinEst = (((edaReadings[i] / vref1) - 1)*Rfeedback);
			Serial.print(RskinEst); Serial.print(" | ");
		}
		vref1 = vRef1;// updated vref1
		vref2 = vRef2;// updated vref2
		Rfeedback = Rfb;// updated edaFeedbackAmpR
		Serial.print("\nEstimated Rskin values AFTER correction: |");
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			RskinEst = (((edaReadings[i] / vref1) - 1)*Rfeedback);
			Serial.print(RskinEst); Serial.print(" | ");
		}
		if (dummyWrite)
		{
			Serial.println("\nupdated emotibit class with these values");
			Serial.println("\nYou can now use this EmotiBit without restarting to measure the EDA test rig values");
		}
		//edaCorrection.correctionDataReady = false; // once the values are updated, we can set it to false to not enter this case again
	progress = EdaCorrection::Progress::FINISH;
	}
}

EdaCorrection::Mode EdaCorrection::getMode()
{
	return _mode;
}

void EdaCorrection::getFloatFromString()
{
	float input[2 * NUM_EDL_READINGS];
	for (int i = 0; i < 2 * NUM_EDL_READINGS; i++)
	{
		String splitString = Serial.readStringUntil(',');
		input[i] = splitString.toFloat();
	}

	for (int i = 0; i < NUM_EDL_READINGS; i++)
	{
		edaReadings[i] = input[i];
	}

	for (int i = NUM_EDL_READINGS; i < 2 * NUM_EDL_READINGS; i++)
	{
		vref2Readings[i - NUM_EDL_READINGS] = input[i];
	}

}

EdaCorrection::Status EdaCorrection::monitorSerial()
{

	if (progress == EdaCorrection::Progress::WAITING_FOR_SERIAL_DATA)
	{
		if (Serial.available())
		{
			//ToDo: implement a check if the input is indeed 5 float values
			Serial.println("Serial data detected. Reading from Serial");
			getFloatFromString();
			echoEdaReadingsOnScreen();
			progress = EdaCorrection::Progress::WAITING_USER_APPROVAL;
		}
	}
	else if (progress == EdaCorrection::Progress::WAITING_USER_APPROVAL)
	{
		if (_responseRecorded == false)
		{
			getUserApproval();
		}
		else
		{
			if (getApprovalStatus() == true)
			{
				Serial.println("#### GOT APPROVAL ####");
				progress = EdaCorrection::Progress::WRITING_TO_OTP;
			}
			else
			{
				//ToDo: user denied writing to OTP
				Serial.println("back to Progress::WAITING_FOR_SERIAL_DATA");
				Serial.println("Enter the eda values into the serial monitor");
				progress = EdaCorrection::Progress::WAITING_FOR_SERIAL_DATA;
				_responseRecorded = false;
			}
		}
	}

}


void EdaCorrection::echoEdaReadingsOnScreen()
{
	Serial.println("The EDA values entered by the user are:");

	Serial.println("EDL readings:");
	for (int i = 0; i < NUM_EDL_READINGS; i++)
	{
		Serial.print(edaReadings[i], 6); Serial.print("\t");
	}
	Serial.println("\nVref2 readings:");
	for (int i = 0; i < NUM_EDL_READINGS; i++)
	{
		Serial.print(vref2Readings[i], 6); Serial.print("\t");
	}

	//_EdaReadingsPrinted = true;
	Serial.println("\nProceed with these values?");
	Serial.println("Enter Y for yes and N to enter data again");

}


bool EdaCorrection::getUserApproval()
{
	if (Serial.available())
	{
		char response = Serial.read();
		if (response == 'Y')
		{
			setApprovalStatus(true);
			_responseRecorded = true;
		}
		else if (response == 'N')
		{
			setApprovalStatus(false);
			_responseRecorded = true;
		}
		else
		{
			Serial.println("incorrect choice. Please enter either Y for Yes or N for No");
		}

	}
}

void EdaCorrection::setApprovalStatus(bool response)
{
	_approvedToWriteOtp = response;
}


bool EdaCorrection::getApprovalStatus()
{
	return _approvedToWriteOtp;
}

EdaCorrection::Status EdaCorrection::writeToOtp(TwoWire* emotiBit_i2c, uint8_t addr, char val)
{
	emotiBit_i2c->beginTransmission(_si7013Addr);
	emotiBit_i2c->write(SI_7013_CMD_OTP_WRITE);
	emotiBit_i2c->write(addr);
	emotiBit_i2c->write(val);
	emotiBit_i2c->endTransmission();
}

EdaCorrection::Status EdaCorrection::writeToOtp(TwoWire* emotiBit_i2c)
{ 
	if (dummyWrite)
	{
		// Serial.println("DUMMY DUMMY DUMMY");
		union Data {
			float edaReading; 
			char byte[4];// buffer to store the float in BYTE form
		}data;
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			data.edaReading = edaReadings[i];
			for (int j = 0; j < 4; j++)
			{
				dummyOtp[i * 4 + j] = data.byte[j];
			}
		}
		_mode = EdaCorrection::Mode::NORMAL;
	}
	else
	{
		if (progress == EdaCorrection::Progress::WRITING_TO_OTP && triedRegOverwrite == false)
		{
			// Serial.println("#######WRITING TO OTP######");

			union Data {
				float floatValue;
				char byte[4];// buffer to store the float in BYTE form
			}data;

#ifdef ACCESS_MAIN_ADDRESS
			for (uint8_t i = 0; i < NUM_EDL_READINGS; i++)
			{
				data.floatValue = edaReadings[i];
				for (uint8_t j = 0; j < 4; j++)
				{
					if (!isOtpRegWritten(emotiBit_i2c, SI_7013_OTP_ADDRESS_EDL_TABLE + 4 * i + j))
					{
						writeToOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_EDL_TABLE + 4 * i + j, data.byte[j]);
					}
					else
					{
						triedRegOverwrite = true;
						_mode = EdaCorrection::Mode::NORMAL;
						return EdaCorrection::Status::FAILURE;
					}
				}
			}

			// writing the vref 2 value
			float vRef2_temp = 0;
			for (int i = 0; i < NUM_EDL_READINGS; i++)
			{
				vRef2_temp += vref2Readings[i];
			}
			vRef2_temp = vRef2_temp / NUM_EDL_READINGS;
			data.floatValue = vRef2_temp;
			for (uint8_t j = 0; j < 4; j++)
			{
				if (!isOtpRegWritten(emotiBit_i2c, SI_7013_OTP_ADDRESS_VREF2 + j))
				{
					writeToOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_VREF2 + j, data.byte[j]);
				}
				else
				{
					triedRegOverwrite = true;
					_mode = EdaCorrection::Mode::NORMAL;
					return EdaCorrection::Status::FAILURE;
				}
			}

			// writing the metadata
			if (!isOtpRegWritten(emotiBit_i2c, SI_7013_OTP_ADDRESS_METADATA) && !isOtpRegWritten(emotiBit_i2c, SI_7013_OTP_ADDRESS_METADATA + 1))
			{
				writeToOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_METADATA, (uint8_t)otpDataFormat);
				writeToOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_METADATA + 1, (uint8_t)emotiBitVersion);
			}
			else
			{
				triedRegOverwrite = true;
				_mode = EdaCorrection::Mode::NORMAL;
				return EdaCorrection::Status::FAILURE;
			}

#else
			
			for (uint8_t i = 0; i < 2; i++)//count first 2 readings from the EDA array
			{
				data.floatValue = edaReadings[i];
				for (uint8_t j = 0; j < 4; j++)// count 4 bytes
				{
					if (!isOtpRegWritten(emotiBit_i2c, SI_7013_OTP_ADDRESS_TEST_1 + 4 * i + j))
					{
						writeToOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_TEST_1 + 4 * i + j, data.byte[j]);
					}
					else
					{
						triedRegOverwrite = true;
						_mode = EdaCorrection::Mode::NORMAL;
						return EdaCorrection::Status::FAILURE;
					}
				}
			}
#endif
		}

		// after writing to the OTP, change the mode to normal
		_mode = EdaCorrection::Mode::NORMAL;
	}
}


uint8_t EdaCorrection::readFromOtp(TwoWire* emotiBit_i2c, uint8_t addr)
{
	emotiBit_i2c->beginTransmission(_si7013Addr);
	emotiBit_i2c->write(SI_7013_CMD_OTP_READ);
	emotiBit_i2c->write(addr);
	emotiBit_i2c->endTransmission();
	emotiBit_i2c->requestFrom(_si7013Addr, 1);
	if (emotiBit_i2c->available())
	{
		return(emotiBit_i2c->read());
	}
}

EdaCorrection::Status EdaCorrection::readFromOtp(TwoWire* emotiBit_i2c)
{
	if (dummyWrite)
	{
		union Data {
			float edaReading; 
			char byte[4];// buffer to store the float in BYTE form
		}data;
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			for (int j = 0; j < 4; j++)
			{
				data.byte[j] = dummyOtp[i * 4 + j];
			}
			edaReadings[i] = data.edaReading;
		}
	}
	else
	{
		union Data {
			float floatValue; 
			char byte[4];// buffer to store the float in BYTE form
		}data;
#ifdef ACCESS_MAIN_ADDRESS
		if ((uint8_t)readFromOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_METADATA) == 255)
		{
			isOtpValid = false;
			readOtpValues = true;
			return EdaCorrection::Status::FAILURE;
		}
		
		for (uint8_t i = 0; i < NUM_EDL_READINGS; i++)
		{
			for (uint8_t j = 0; j < 4; j++)
			{
				data.byte[j] = readFromOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_EDL_TABLE + 4 * i + j);
			}
			edaReadings[i] = data.floatValue;
		}
		for (uint8_t j = 0; j < 4; j++)
		{
			data.byte[j] = readFromOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_VREF2 + j);
		}
		vRef2 = data.floatValue;
#else
		for (uint8_t i = 0; i < 2; i++)
		{
			for (uint8_t j = 0; j < 4; j++)
			{
				data.byte[j] = readFromOtp(emotiBit_i2c, SI_7013_OTP_ADDRESS_TEST_1 + 4 * i + j);
			}
			edaReadings[i] = data.floatValue;
		}
#endif

	}
	readOtpValues = true;
}

bool EdaCorrection::isOtpRegWritten(TwoWire* emotiBit_i2c, uint8_t addr)
{
	if ((uint8_t)readFromOtp(emotiBit_i2c, addr) == 255)
	{
		return false;
	}
	else
	{
		return true;
	}
}

EdaCorrection::Status EdaCorrection::calcEdaCorrection()
{
	// perform a check to see if the last byte is written to
	if (dummyWrite)
	{
		Serial.println("The values stored on the mock OTP are:");
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			Serial.print("edaReadings["); Serial.print(i); Serial.print("]: "); Serial.println(edaReadings[i], 6);
		}
		Serial.println("### Calculating values ####\n");
		vRef1 = edaReadings[0];
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			vRef2 += vref2Readings[i];
		}
		vRef2 = vRef2 / NUM_EDL_READINGS;
		Rfb = 0;
		for (int i = 1; i < 4; i++)
		{
			Rfb += (trueRskin[i] / ((edaReadings[i] / vRef1) - 1)); // use the EDl @10K, @100K, @1M
		}
		Rfb = Rfb / 3;// taking avg of 3 readings
		Serial.print("Vref1: "); Serial.println(vRef1, 6);
		Serial.print("Vref2: "); Serial.println(vRef2, 6);
		Serial.print("Rfb: "); Serial.println(Rfb, 6);
		correctionDataReady = true;
	}
	else
	{
#ifdef ACCESS_MAIN_ADDRESS
		Serial.println("The values stored on the Main sensor OTP are:");
		for (int i = 0; i < NUM_EDL_READINGS; i++)
		{
			Serial.print("edaReadings["); Serial.print(i); Serial.print("]: "); Serial.println(edaReadings[i], 6);
		}
		Serial.println("### Calculating values ####\n");
		vRef1 = edaReadings[0];
		//vRef2 is updated directly in the readFromOtp() function
		Rfb = 0;
		for (int i = 1; i < 4; i++)
		{
			Rfb += (trueRskin[i] / ((edaReadings[i] / vRef1) - 1)); // use the EDl @10K, @100K, @1M
		}
		Rfb = Rfb / 3;// taking avg of 3 readings
		Serial.print("Vref1: "); Serial.println(vRef1, 6);
		Serial.print("Vref2: "); Serial.println(vRef2, 6);
		Serial.print("Rfb: "); Serial.println(Rfb, 6);
		correctionDataReady = true;
#else
		Serial.println("No claculations performed. Just reading values read from the OTP of the Alternate SI chip.");
		for (int i = 0; i < 2; i++)
		{
			Serial.print("edaReadings["); Serial.print(i); Serial.print("]: "); Serial.println(edaReadings[i], 6);
		}
#endif
	}

	calculationPerformed = true;
}

