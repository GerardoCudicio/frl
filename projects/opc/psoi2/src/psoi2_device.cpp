#include "frl_platform.h"
#if( FRL_PLATFORM == FRL_PLATFORM_WIN32 )
#include <time.h>
#include <bitset>
#include "psoi2_device.h"
#include "frl_types.h"
#include "frl_lexical_cast.h"
#include "stream_std/frl_sstream.h"

using namespace frl;
using namespace frl::opc;

Psoi2Device::Psoi2Device(	frl::UInt portNumber_,
										frl::UInt channelsNumber_,
										frl::Bool simulation_,
										frl::logging::Level logLevel,
										const frl::String &logFileNamePrefix	)
	:	portNumber( portNumber_ ),
		bytesNumber( channelsNumber_ * 2 ),
		simulation( simulation_ ),
		channelsNumber( channelsNumber_ )
{
	String portName = FRL_STR("COM_");
	portName += lexicalCast< frl::Int, frl::String >( portNumber );
	log.setName( logFileNamePrefix + portName );
	log.setLevel( logLevel );
	log.setFormat(	frl::String(FRL_STR("[")) 
							<< frl::logging::DateElement()
							<< frl::String(FRL_STR("; ") )
							<< frl::logging::TimeElement()
							<< frl::String(FRL_STR("] ") )
							<< frl::logging::MsgElement()
							<< frl::logging::EndlElement() );
	log.addDestination( frl::logging::ConsoleWriter() );
	log.addDestination( frl::logging::FileWriter() );
	FRL_LOG_INFO( log ) << FRL_STR("====Start log for ") << portName <<FRL_STR(" =====") ;
	try
	{
		opcAddressSpace::getInstance().addBranch( portName );
	}
	catch( address_space::Tag::IsExistTag )
	{
		FRL_LOG_ERROR(log ) << FRL_STR("������ �������� ����� � �������� ������������ OPC �������\
															- ������ ��������� ������������ COM ����� � ����������");
		String msg = FRL_STR("Redoubling COM port# ") + lexicalCast< frl::Int, frl::String >( portNumber );
		::MessageBox( NULL,
			msg.c_str(),
			FRL_STR("OPC server configuration error!"),
			MB_OK | MB_ICONERROR );
		exit( 1 );
	}

	portName += opcAddressSpace::getInstance().getDelimiter();
	String low = portName + FRL_STR( "channel_0" ); // COM_X.channel_0X
	String hight = portName + FRL_STR( "channel_" ); // COM_X.channel_XX

	// Initialize branch in address space
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			opcAddressSpace::getInstance().addBranch( low + lexicalCast< int, frl::String >( i ) );
		else
			opcAddressSpace::getInstance().addBranch( hight + lexicalCast<int, frl::String>( i ) );
	}

	channels.resize( channelsNumber );

	setUpValues( low, hight );
	setUpThresholdExceeding( low, hight );
	setUpTypePPC( low, hight );
	setUpMGC( low, hight );
	setUpPPC( low, hight );
	
	if( ! simulation )
	{
		try
		{
			using namespace frl::io::comm_ports;
			FRL_LOG_TRACE(log) << FRL_STR("������� ������� COM ���� �") << portNumber ;
			comPort.open( portNumber );
			FRL_LOG_TRACE(log) << FRL_STR("���� ������");
			FRL_LOG_TRACE(log) << FRL_STR("��������� �����");
			comPort.setup( serial::BAUD_RATE_1200, serial::CHAR_SIZE_8, serial::EPARITY_NONE, serial::STOP_BITS_1 );
			FRL_LOG_TRACE(log) << FRL_STR("�������� �� �������");
			comPort.setEventMask( serial::EVENT_CTS | serial::EVENT_DSR | serial::EVENT_ERROR | serial::EVENT_RING | serial::EVENT_RLSD | serial::EVENT_RECV );
			comPort.setReadTimeouts( serial::READ_TIMEOUT_NON_BLOCK );
			FRL_LOG_TRACE(log) << FRL_STR("���� ��������");
		}
		catch( frl::Exception& ex )
		{
			String msg = FRL_STR("Unable open COM port#") + lexicalCast< frl::Int, frl::String >( portNumber );
			FRL_LOG_ERROR( log ) << msg;
			FRL_LOG_ERROR( log ) << ex.getFullDescription();
			::MessageBox( NULL,
				msg.c_str(),
				FRL_STR("OPC server starting error!"),
				MB_OK | MB_ICONERROR );
			exit( 1 );
		}
	}
}
Psoi2Device::~Psoi2Device()
{

}

void Psoi2Device::setUpValues( const frl::String &low, const frl::String &hight )
{
	// set up values
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			channels[i].value = opcAddressSpace::getInstance().addLeaf( low
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("value") ) ;
		else
			channels[i].value = opcAddressSpace::getInstance().addLeaf( hight
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("value") );
		channels[i].value->isWriteable( False );
		channels[i].value->setCanonicalDataType( VT_R4 );
		channels[i].value->write( 0.0f );
	}
}

void Psoi2Device::setUpThresholdExceeding( const frl::String &low, const frl::String &hight )
{
	// set up threshold exceeding
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			channels[i].thresholdExceeding = opcAddressSpace::getInstance().addLeaf( low
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("thresholdExceeding") ) ;
		else
			channels[i].thresholdExceeding = opcAddressSpace::getInstance().addLeaf( hight
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("thresholdExceeding") );
		channels[i].thresholdExceeding->isWriteable( False );
		channels[i].thresholdExceeding->setCanonicalDataType( VT_UI4 );
		channels[i].thresholdExceeding->write( 0 );
	}
}

void Psoi2Device::setUpTypePPC( const frl::String &low, const frl::String &hight )
{
	// set up type PPC
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			channels[i].typePPC = opcAddressSpace::getInstance().addLeaf( low
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("type") ) ;
		else
			channels[i].typePPC = opcAddressSpace::getInstance().addLeaf( hight
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("type") );
		channels[i].typePPC->isWriteable( False );
		channels[i].typePPC->setCanonicalDataType( VT_UI4 );
		channels[i].typePPC->write( 0 );
	}
}

void Psoi2Device::setUpMGC( const frl::String &low, const frl::String &hight )
{
	// set up state of micro generator chlorine
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			channels[i].goodMGC = opcAddressSpace::getInstance().addLeaf( low
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("goodMGC") ) ;
		else
			channels[i].goodMGC = opcAddressSpace::getInstance().addLeaf( hight
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("goodMGC") );
		channels[i].goodMGC->isWriteable( False );
		channels[i].goodMGC->setCanonicalDataType( VT_UI4 );
		channels[i].goodMGC->write( 0 );
	}
}

void Psoi2Device::setUpPPC( const frl::String &low, const frl::String &hight )
{
	// set up state PPC
	for( frl::UInt i = 0; i < channelsNumber; i++ )
	{
		if ( i < 10)
			channels[i].goodPPC = opcAddressSpace::getInstance().addLeaf( low
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("goodPPC") ) ;
		else
			channels[i].goodPPC = opcAddressSpace::getInstance().addLeaf( hight
			+ lexicalCast<int, frl::String>( i )
			+ opcAddressSpace::getInstance().getDelimiter()
			+ FRL_STR("goodPPC") );
		channels[i].goodPPC->isWriteable( False );
		channels[i].goodPPC->setCanonicalDataType( VT_UI4 );
		channels[i].goodPPC->write( 0 );
	}
}

void Psoi2Device::startProcess()
{
	if( simulation )
		processThread.create( &Psoi2Device::simulationProcess, *this );
	else
		processThread.create( &Psoi2Device::workProcess, *this );
	processThread.start();
}

void Psoi2Device::simulationProcess()
{
	FRL_LOG_TRACE( log ) << FRL_STR("������ ��������-����������" );
	while( 1 )
	{
		::Sleep( 2000 );
		for( frl::UInt i = 0; i < getChannelsNumber(); ++i )
		{
			float value = (float)rand();
			value = ( value * 100 ) / rand();
			setValue( i, value );
		}
	}
}

void Psoi2Device::workProcess()
{
	FRL_LOG_TRACE( log ) << FRL_STR("������ �������� ������ ������" );
	using namespace io::comm_ports::serial;
	char readBuffer[ 101 ] = {'\0'};
	SerialRWCount counts;

	do
	{
		counts = 0;
		try
		{
			FRL_LOG_TRACE( log ) << FRL_STR("�������� ������" );
			comPort.waitEvent();
			if( comPort.getLastEvent() == io::comm_ports::serial::EVENT_RECV )
			{
				Sleep( 1000 ); // ���� ����� ��� ����� ������
				FRL_LOG_TRACE( log ) << FRL_STR("����� ������" );
				counts = comPort.read( readBuffer, sizeof( readBuffer ) - 1 );
				FRL_LOG_TRACE( log ) << FRL_STR("������ �������: " ) << counts 
													<< FRL_STR(" ����");
			}
			else
			{
				FRL_LOG_WARN( log ) << FRL_STR("������ �� ������, � �������: " )
												<< comPort.getLastEvent();
				continue;
			}
		}
		catch( frl::Exception& ex)
		{
			FRL_LOG_ERROR( log ) << ex.getFullDescription();
			continue;
		}
		
		if( counts == 0 )
		{
			FRL_LOG_TRACE( log ) << FRL_STR("������� ������� ����� - ������ ��� ��������� �����������");
			continue;
		}
		
		if( counts > getBytesNumber() + 2 )
		{
			FRL_LOG_WARN( log ) << FRL_STR("����� ������� �������. ��������� ������� �� ")
											<< getBytesNumber()
											<< FRL_STR("����. �������� ������������ ")
											<< (counts - 2) / 2
											<< FRL_STR(" ��������� ����2");
			continue;
		}
		
		if( readBuffer[1] != (Int)getBytesNumber() )
		{
			FRL_LOG_WARN( log ) << FRL_STR( "������� ���������� �����, �� 2 ���� ������������ ������� � " )
											<< readBuffer[1] << FRL_STR(" ����. (?)");
			continue;
		}
		
		readBuffer[ counts ] = '\0';
		int byteRead = -2;
		std::vector< std::bitset<8> > pure_array;
		pure_array.resize( getBytesNumber() );
		Bool transfer = False;
		for( SerialRWCount i = 0; i < counts; i++ )
		{
			std::bitset<8> byteTmp( readBuffer[i] );
			if( byteTmp.to_ulong() == 251 ) // ���� ������ �������
			{
				FRL_LOG_TRACE( log ) << FRL_STR("������� ������ �������" );
				transfer = true;
				byteRead = -2;
			}

			if( byteRead >= 0 && byteRead < (int)getBytesNumber() )
			{
				pure_array[byteRead] = byteTmp;
			}
			if( transfer )
				++byteRead;
		}
		if( ( byteRead == (int)getBytesNumber() )  && transfer )
		{
			FRL_LOG_TRACE( log ) << FRL_STR("������ ������" );
			fillValues( pure_array );
		}
		else
		{
			FRL_LOG_WARN( log ) << FRL_STR("������ �� ���� ���������� - �������� ������ ������" );
		}
	}
	while( 1 );
}

void Psoi2Device::fillValues( const std::vector< std::bitset<8> > &pure_array )
{
	// ������ ��� ����-�����
	std::vector< std::bitset< 4 > > pure_bite;
	pure_bite.resize( getBytesNumber() * 2 );
	
	FRL_LOG_TRACE( log ) << FRL_STR("��������� ���������� �������" );
	for( std::vector< std::bitset< 4 > >::iterator it = pure_bite.begin(); it != pure_bite.end(); ++it )
		*it = 0;
	
	FRL_LOG_TRACE( log ) << FRL_STR("�������� �� ����-�����" );
	// ��������� �� ����-�����
	for( frl::UInt i = 0, j = 0; i < getBytesNumber(); ++i, j+=2)
	{	
		for( int r = 0; r < 4; ++r)
		{
			pure_bite[j][r] = pure_array[i][r+4];
			pure_bite[j+1][r] = pure_array[i][r];
		}
	}
	
	String tmpSt;
	std::vector< float > channel_value( getChannelsNumber() );
	
	FRL_LOG_TRACE( log ) << FRL_STR("���������� pure �������" );
	for( frl::UInt i = 0, j = 0 ; i < getChannelsNumber(); ++i, j = j+4 )
	{
		for( int k =0; k < 3; ++k )
		{
			tmpSt += lexicalCast< unsigned long, String >( pure_bite[ j + k ].to_ulong() );
		}
		channel_value[i] = lexicalCast< String, float >( tmpSt );
		tmpSt.clear();
	}
	
	FRL_LOG_TRACE( log ) << FRL_STR("�������� ��������� ��������" );
	std::vector< float > values( getChannelsNumber() );
	std::vector< frl::Bool > pph_type( getChannelsNumber() );
	std::vector< frl::Bool > pph_accuracy( getChannelsNumber() );
	std::vector< frl::Bool > overr_crossing( getChannelsNumber() );
	std::vector< frl::Bool > gen_accuracy( getChannelsNumber() );

	FRL_LOG_TRACE( log ) << FRL_STR("���������� ��������� ��������" );
	// ��������� ��� �������
	for( frl::UInt i =0, j=3; i < getChannelsNumber(); ++i, j+=4 )
	{
		pph_accuracy[i] = pure_bite[j][0];	// ����������� ���
		pph_type[i] = pure_bite[j][1];			// ��� ���
		gen_accuracy[i] = pure_bite[j][2];	// ����������� ��������������� �����
		overr_crossing[i] = pure_bite[j][3];	// ���������� ���������		

		// ��������� �������� ������������ � ����������� �� ���� ���
		if (pph_type[i] == 0)				
			channel_value[i] = channel_value[i]/10.0f;
		else
			channel_value[i] = channel_value[i]/100.0f;
	}
	
	FRL_LOG_TRACE( log ) << FRL_STR("��������� �������� �����" );
	for( frl::UInt i = 0; i < getChannelsNumber(); ++i )
	{
		setValue( i, channel_value[i] );
		FRL_LOG_TRACE( log ) << FRL_STR("�������� ������ �") << i <<  FRL_STR("==") << channel_value[i];
		setGoodPPC( i, pph_accuracy[i] );
		FRL_LOG_TRACE( log ) << FRL_STR("����������� ��� ������ �") << i <<  FRL_STR("==") << pph_accuracy[i];
		setGoodMGC(i, gen_accuracy[i] );
		FRL_LOG_TRACE( log ) << FRL_STR("����������� ��� ������ �") << i <<  FRL_STR("==") << gen_accuracy[i];
		setThresholdExceeding( i, overr_crossing[i] );
		FRL_LOG_TRACE( log ) << FRL_STR("���������� ����� ������ �") << i <<  FRL_STR("==") << overr_crossing[i];
		setTypePPC( i, pph_type[i] );
		FRL_LOG_TRACE( log ) << FRL_STR("��� ��� ������ �") << i <<  FRL_STR("==") << pph_type[i];
	}
}

frl::UInt Psoi2Device::getChannelsNumber()
{
	return channelsNumber;
}

frl::UInt Psoi2Device::getBytesNumber()
{
	return bytesNumber;
}

void Psoi2Device::setValue( frl::UInt number, float value )
{
	channels[ number ].value->write( value );
}

void Psoi2Device::setTypePPC( frl::UInt number, frl::Bool type )
{
	channels[ number ].typePPC->write( type );
}

void Psoi2Device::setGoodPPC( frl::UInt number, frl::Bool goodFlag )
{
	channels[ number ].goodPPC->write( goodFlag );
}

void Psoi2Device::setGoodMGC( frl::UInt number, frl::Bool goodFlag )
{
	channels[ number ].goodMGC->write( goodFlag );
}

void Psoi2Device::setThresholdExceeding( frl::UInt number, frl::Bool thresholdExceedingFlag )
{
	channels[ number ].thresholdExceeding->write( thresholdExceedingFlag );
}
#endif // FRL_PLATFORM_WIN32
