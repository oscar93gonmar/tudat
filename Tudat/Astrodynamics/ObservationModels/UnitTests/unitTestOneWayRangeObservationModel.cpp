/*    Copyright (c) 2010-2017, Delft University of Technology
 *    All rigths reserved
 *
 *    This file is part of the Tudat. Redistribution and use in source and
 *    binary forms, with or without modification, are permitted exclusively
 *    under the terms of the Modified BSD license. You should have received
 *    a copy of the license with this file. If not, please or visit:
 *    http://tudat.tudelft.nl/LICENSE.
 */

#define BOOST_TEST_MAIN

#include <limits>
#include <string>

#include <boost/format.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/make_shared.hpp>

#include "Tudat/Basics/testMacros.h"

#include "Tudat/InputOutput/basicInputOutput.h"

#include "Tudat/SimulationSetup/EnvironmentSetup/body.h"
#include "Tudat/Astrodynamics/ObservationModels/oneWayRangeObservationModel.h"
#include "Tudat/SimulationSetup/EstimationSetup/createObservationModel.h"
#include "Tudat/SimulationSetup/EnvironmentSetup/defaultBodies.h"
#include "Tudat/SimulationSetup/EnvironmentSetup/createBodies.h"

namespace tudat
{
namespace unit_tests
{

using namespace tudat::observation_models;
using namespace tudat::spice_interface;
using namespace tudat::ephemerides;
using namespace tudat::simulation_setup;


BOOST_AUTO_TEST_SUITE( test_one_way_range_model )


BOOST_AUTO_TEST_CASE( testOneWayRangeModel )
{
    std::string kernelsPath = input_output::getSpiceKernelPath( );
    spice_interface::loadSpiceKernelInTudat( kernelsPath + "de-403-masses.tpc");
    spice_interface::loadSpiceKernelInTudat( kernelsPath + "naif0009.tls");
    spice_interface::loadSpiceKernelInTudat( kernelsPath + "pck00009.tpc");
    spice_interface::loadSpiceKernelInTudat( kernelsPath + "de421.bsp");

    // Define bodies to use.
    std::vector< std::string > bodiesToCreate;
    bodiesToCreate.push_back( "Earth" );
    bodiesToCreate.push_back( "Sun" );
    bodiesToCreate.push_back( "Mars" );

    // Specify initial time
    double initialEphemerisTime = 0.0;
    double finalEphemerisTime = initialEphemerisTime + 7.0 * 86400.0;
    double maximumTimeStep = 3600.0;
    double buffer = 10.0 * maximumTimeStep;

    // Create bodies settings needed in simulation
    std::map< std::string, boost::shared_ptr< BodySettings > > defaultBodySettings =
            getDefaultBodySettings(
                bodiesToCreate, initialEphemerisTime - buffer, finalEphemerisTime + buffer );

    // Create bodies
    NamedBodyMap bodyMap = createBodies( defaultBodySettings );

    setGlobalFrameBodyEphemerides( bodyMap, "SSB", "ECLIPJ2000" );

    // Define link ends for observations.
    LinkEnds linkEnds;
    linkEnds[ transmitter ] = std::make_pair( "Earth" , ""  );
    linkEnds[ receiver ] = std::make_pair( "Mars" , ""  );

    // Create light-time correction settings.
    std::vector< std::string > lightTimePerturbingBodies = boost::assign::list_of( "Sun" );
    std::vector< boost::shared_ptr< LightTimeCorrectionSettings > > lightTimeCorrectionSettings;
    lightTimeCorrectionSettings.push_back( boost::make_shared< FirstOrderRelativisticLightTimeCorrectionSettings >(
                                                lightTimePerturbingBodies ) );


    // Create observation settings
    boost::shared_ptr< ObservationSettings > observableSettings = boost::make_shared< ObservationSettings >
            ( one_way_range, lightTimeCorrectionSettings,
              boost::make_shared< ConstantObservationBiasSettings >(
                  ( Eigen::Matrix< double, 1, 1 >( ) <<2.56294 ).finished( ) ) );

    // Create observation model.
    boost::shared_ptr< ObservationModel< 1, double, double > > observationModel =
           ObservationModelCreator< 1, double, double >::createObservationModel(
                linkEnds, observableSettings, bodyMap );
    boost::shared_ptr< ObservationBias< 1 > > observationBias = observationModel->getObservationBiasCalculator( );



    // Compute observation separately with two functions.
    double receiverObservationTime = ( finalEphemerisTime + initialEphemerisTime ) / 2.0;
    std::vector< double > linkEndTimes;
    std::vector< Eigen::Vector6d > linkEndStates;
    double observationFromReceptionTime = observationModel->computeObservations(
                receiverObservationTime, receiver )( 0 );
    double observationFromReceptionTime2 = observationModel->computeObservationsWithLinkEndData(
                receiverObservationTime, receiver, linkEndTimes, linkEndStates )( 0 );
    BOOST_CHECK_EQUAL( linkEndTimes.size( ), 2 );
    BOOST_CHECK_EQUAL( linkEndStates.size( ), 2 );

    // Manually create and compute light time corrections
    boost::shared_ptr< LightTimeCorrection > lightTimeCorrectionCalculator =
            createLightTimeCorrections(
                lightTimeCorrectionSettings.at( 0 ), bodyMap, linkEnds[ transmitter ], linkEnds[ receiver ] );
    double lightTimeCorrection = lightTimeCorrectionCalculator->calculateLightTimeCorrection(
                linkEndStates.at( 0 ), linkEndStates.at( 1 ), linkEndTimes.at( 0 ), linkEndTimes.at( 1 ) );

    // Check equality of computed observations.
    BOOST_CHECK_CLOSE_FRACTION(
                observationFromReceptionTime, observationFromReceptionTime2, std::numeric_limits< double >::epsilon( ) );

    // Check consistency of link end states and time.
    TUDAT_CHECK_MATRIX_CLOSE_FRACTION(
                linkEndStates.at( 0 ), bodyMap.at( "Earth" )->getStateInBaseFrameFromEphemeris( linkEndTimes.at( 0 ) ),
                std::numeric_limits< double >::epsilon( ) );
    TUDAT_CHECK_MATRIX_CLOSE_FRACTION(
                linkEndStates.at( 1 ), bodyMap.at( "Mars" )->getStateInBaseFrameFromEphemeris( linkEndTimes.at( 1 ) ),
                std::numeric_limits< double >::epsilon( ) );

    // Check that reception time is kept fixed.
    BOOST_CHECK_CLOSE_FRACTION( static_cast< double >( receiverObservationTime ),
                                linkEndTimes[ 1 ], std::numeric_limits< double >::epsilon( ) );

    // Manually compute light time
    Eigen::Vector3d positionDifference = ( linkEndStates[ 0 ] - linkEndStates[ 1 ] ).segment( 0, 3 );
    BOOST_CHECK_CLOSE_FRACTION(
                positionDifference.norm( ) / physical_constants::SPEED_OF_LIGHT + lightTimeCorrection,
                linkEndTimes[ 1 ] - linkEndTimes[ 0 ],
                std::numeric_limits< double >::epsilon( ) * 1000.0 );
                        // Poor tolerance due to rounding errors when subtracting times

    // Check computed range from link end states
    BOOST_CHECK_CLOSE_FRACTION(
                positionDifference.norm( ) + lightTimeCorrection * physical_constants::SPEED_OF_LIGHT +
                observationBias->getObservationBias(
                    std::vector< double >( ), std::vector< Eigen::Vector6d >( ) )( 0 ),
                observationFromReceptionTime,
                std::numeric_limits< double >::epsilon( ) );

    // Compute transmission time from light time.
    double transmitterObservationTime = receiverObservationTime - ( linkEndTimes[ 1 ] - linkEndTimes[ 0 ] );

    // Compare computed against returned transmission time.
    BOOST_CHECK_CLOSE_FRACTION(
                static_cast< double >( transmitterObservationTime ), linkEndTimes[ 0 ],
            std::numeric_limits< double >::epsilon( ) );

    // Recompute observable while fixing transmission time.
    std::vector< double > linkEndTimes2;
    std::vector< Eigen::Vector6d > linkEndStates2;
    double observationFromTransmissionTime = observationModel->computeObservations(
                transmitterObservationTime, transmitter )( 0 );
    double observationFromTransmissionTime2 = observationModel->computeObservationsWithLinkEndData(
                transmitterObservationTime, transmitter, linkEndTimes2, linkEndStates2 )( 0 );

    // Compare results against those obtained when keeping reception fixed.
    BOOST_CHECK_SMALL( observationFromTransmissionTime - observationFromTransmissionTime2, 1.0E-15 );
    BOOST_CHECK_SMALL( observationFromTransmissionTime - observationFromReceptionTime, 1.0E-15 );

    for( unsigned int i = 0; i < 2; i++ )
    {
        BOOST_CHECK_CLOSE_FRACTION( linkEndTimes2.at( i ), linkEndTimes2.at( i ), 1.0E-15 );
    }


    // Check consistency of link end states and time.
    TUDAT_CHECK_MATRIX_CLOSE_FRACTION(
                linkEndStates2.at( 0 ), bodyMap.at( "Earth" )->getStateInBaseFrameFromEphemeris( linkEndTimes2.at( 0 ) ),
                std::numeric_limits< double >::epsilon( ) );
    TUDAT_CHECK_MATRIX_CLOSE_FRACTION(
                linkEndStates2.at( 1 ), bodyMap.at( "Mars" )->getStateInBaseFrameFromEphemeris( linkEndTimes2.at( 1 ) ),
                std::numeric_limits< double >::epsilon( ) );
}

BOOST_AUTO_TEST_SUITE_END( )

}

}

