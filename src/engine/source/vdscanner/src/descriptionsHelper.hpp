/*
 * Wazuh Vulnerability scanner
 * Copyright (C) 2015, Wazuh Inc.
 * September 22, 2024.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */
#ifndef _DESCRIPTIONS_HELPER_HPP
#define _DESCRIPTIONS_HELPER_HPP

#include <databaseFeedManager.hpp>
#include <base/json.hpp>
#include <string>

/**
 * @brief Holds information about a vulnerability's CVSS metrics and related data.
 */
struct CveDescription final
{
    /**
     * @brief Complexity of access required to exploit the vulnerability (CVSS metric).
     */
    std::string_view accessComplexity;

    /**
     * @brief Short name of the entity that assigned the CVE.
     */
    std::string_view assignerShortName;

    /**
     * @brief The context by which vulnerability exploitation is possible (CVSS metric).
     */
    std::string_view attackVector;

    /**
     * @brief Level of authentication needed to exploit the vulnerability (CVSS metric).
     */
    std::string_view authentication;

    /**
     * @brief Impact on the availability of the target system (CVSS metric).
     */
    std::string_view availabilityImpact;

    /**
     * @brief The classification or category of the vulnerability.
     */
    std::string_view classification;

    /**
     * @brief Impact on the confidentiality of the target system (CVSS metric).
     */
    std::string_view confidentialityImpact;

    /**
     * @brief Common Weakness Enumeration (CWE) identifier for the vulnerability.
     */
    std::string_view cweId;

    /**
     * @brief Date when the vulnerability was first published.
     */
    std::string_view datePublished;

    /**
     * @brief Date when the vulnerability was last updated.
     */
    std::string_view dateUpdated;

    /**
     * @brief Detailed description of the vulnerability.
     */
    std::string_view description;

    /**
     * @brief Impact on the integrity of the target system (CVSS metric).
     */
    std::string_view integrityImpact;

    /**
     * @brief Level of privileges required to exploit the vulnerability (CVSS metric).
     */
    std::string_view privilegesRequired;

    /**
     * @brief Reference URL or document related to the vulnerability.
     */
    std::string_view reference;

    /**
     * @brief Scope of impact once the vulnerability is exploited (CVSS metric).
     */
    std::string_view scope;

    /**
     * @brief Base CVSS score indicating the severity of the vulnerability.
     * @details Initialized to 0.0 by default.
     */
    float scoreBase = 0.0f;

    /**
     * @brief The version of the CVSS scoring system used.
     */
    std::string_view scoreVersion;

    /**
     * @brief Severity level of the vulnerability (e.g., Low, Medium, High).
     */
    std::string_view severity;

    /**
     * @brief Indicates if user interaction is required to exploit the vulnerability (CVSS metric).
     */
    std::string_view userInteraction;
};

/**
 * @brief Descriptions helper class.
 */
class DescriptionsHelper final
{
private:
    template<typename TDatabaseFeedManager = DatabaseFeedManager>
    static std::pair<const std::string, const std::string>
    cvssAndDescriptionSources(const std::pair<std::string, std::string>& sources,
                              std::shared_ptr<TDatabaseFeedManager>& databaseFeedManager)
    {
        // Ex. sources = {"redhat", "redhat_8"}
        const auto& [adp, expandedAdp] = sources;
        const auto& vendorsMap = databaseFeedManager->vendorsMap();

        nlohmann::json vendorConfig;
        if (vendorsMap.at(ADP_DESCRIPTIONS_MAP_KEY).contains(adp))
        {
            vendorConfig = vendorsMap.at(ADP_DESCRIPTIONS_MAP_KEY).at(adp);
        }
        else
        {
            // Fallback to default ADP
            vendorConfig = vendorsMap.at(ADP_DESCRIPTIONS_MAP_KEY).at(DEFAULT_ADP);
        }

        const auto& cvssSource = vendorConfig.at(ADP_CVSS_KEY).get_ref<const std::string&>();
        const auto& descriptionSource = vendorConfig.at(ADP_DESCRIPTION_KEY).get_ref<const std::string&>();

        return {cvssSource == adp ? expandedAdp : cvssSource,
                descriptionSource == adp ? expandedAdp : descriptionSource};
    }

public:
    /**
     * @brief Get the vulnerability description and CVSS metrics for a given CVE.
     *
     * @note Attempt to retrieve the information from the specified sources. If the information is not available (or it
     * is not reliable), it uses the default ADP information instead.
     *
     * @tparam TDatabaseFeedManager Database feed manager type.
     *
     * @param cve CVE identifier.
     * @param sources Pair of sources (ADP and expanded ADP).
     * @param databaseFeedManager Database feed manager instance.
     * @param callback Callback function to call with the retrieved CveDescription object.
     *
     */
    template<typename TDatabaseFeedManager = DatabaseFeedManager>
    static void vulnerabilityDescription(const std::string& cve,
                                         const std::pair<std::string, std::string>& sources,
                                         std::shared_ptr<TDatabaseFeedManager>& databaseFeedManager,
                                         const std::function<void(const CveDescription&)>& callback)
    {
        FlatbufferDataPair<NSVulnerabilityScanner::VulnerabilityDescription> descriptionData;
        FlatbufferDataPair<NSVulnerabilityScanner::VulnerabilityDescription> cvssData;

        const auto [cvssSource, descriptionSource] =
            DescriptionsHelper::cvssAndDescriptionSources(sources, databaseFeedManager);

        // Get description data

        // Check if the description information is reliable
        // The description information is considered unreliable if the description is empty or "not defined"
        const auto descriptionIsReliable = [&descriptionData]()
        {
            if (!descriptionData.data->description() || descriptionData.data->description()->str() == "not defined")
            {
                return false;
            }

            return true;
        };

        if (!databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, descriptionSource, descriptionData))
        {
            // Information not from source, try with default ADP
            databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, DEFAULT_ADP, descriptionData);
        }

        if (!descriptionIsReliable() && descriptionSource != DEFAULT_ADP)
        {
            databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, DEFAULT_ADP, descriptionData);
        }

        // Get CVSS data

        // Check if the CVSS information is reliable
        // The CVSS information is considered unreliable if the score is near 0 or the severity is empty
        const auto cvssIsReliable = [&cvssData, &cvssSource]()
        {
            if (cvssData.data->scoreBase() < 0.01f || cvssData.data->severity()->str().empty())
            {
                return false;
            }

            return true;
        };

        if (cvssSource != descriptionSource)
        {
            if (!databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, cvssSource, cvssData))
            {
                // Information not from source, try with default ADP
                databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, DEFAULT_ADP, cvssData);
            }
        }
        else
        {
            // If the sources are the same, cvssData will be the same as descriptionData
            cvssData.data = descriptionData.data;
        }

        if (!cvssIsReliable() && cvssSource != DEFAULT_ADP)
        {
            databaseFeedManager->getVulnerabiltyDescriptiveInformation(cve, DEFAULT_ADP, cvssData);
        }

        // Call the callback function with the CveDescription object
        callback(CveDescription {cvssData.data->accessComplexity()->string_view(),
                                 descriptionData.data->assignerShortName()->string_view(),
                                 cvssData.data->attackVector()->string_view(),
                                 cvssData.data->authentication()->string_view(),
                                 cvssData.data->availabilityImpact()->string_view(),
                                 cvssData.data->classification()->string_view(),
                                 cvssData.data->confidentialityImpact()->string_view(),
                                 descriptionData.data->cweId()->string_view(),
                                 descriptionData.data->datePublished()->string_view(),
                                 descriptionData.data->dateUpdated()->string_view(),
                                 descriptionData.data->description()->string_view(),
                                 cvssData.data->integrityImpact()->string_view(),
                                 cvssData.data->privilegesRequired()->string_view(),
                                 descriptionData.data->reference()->string_view(),
                                 cvssData.data->scope()->string_view(),
                                 cvssData.data->scoreBase(),
                                 cvssData.data->scoreVersion()->string_view(),
                                 cvssData.data->severity()->string_view(),
                                 cvssData.data->userInteraction()->string_view()});
    }
};

#endif // _DESCRIPTIONS_HELPER_HPP
