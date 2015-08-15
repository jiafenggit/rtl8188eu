/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTW_PWRCTRL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <osdep_intf.h>

#ifdef CONFIG_BT_COEXIST
#include <rtl8723a_hal.h>
#endif

void _ips_enter(struct adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	if (padapter->hw_init_completed == false) {
		DBG_871X("%s: hw_init_completed: %d\n",
			__func__, padapter->hw_init_completed);
		return;
	}

	pwrpriv->bips_processing = true;

	/*  syn ips_mode with request */
	pwrpriv->ips_mode = pwrpriv->ips_mode_req;

	pwrpriv->ips_enter_cnts++;
	DBG_871X("==>ips_enter cnts:%d\n",pwrpriv->ips_enter_cnts);
#ifdef CONFIG_BT_COEXIST
	BTDM_TurnOffBtCoexistBeforeEnterIPS(padapter);
#endif
	if (rf_off == pwrpriv->change_rfpwrstate )
	{
		pwrpriv->bpower_saving = true;
		DBG_871X_LEVEL(_drv_always_, "nolinked power save enter\n");

		if (pwrpriv->ips_mode == IPS_LEVEL_2)
			pwrpriv->bkeepfwalive = true;

		rtw_ips_pwr_down(padapter);
		pwrpriv->rf_pwrstate = rf_off;
	}
	pwrpriv->bips_processing = false;

}

void ips_enter(struct adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	_enter_pwrlock(&pwrpriv->lock);
	_ips_enter(padapter);
	_exit_pwrlock(&pwrpriv->lock);
}

int _ips_leave(struct adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int result = _SUCCESS;

	if ((pwrpriv->rf_pwrstate == rf_off) &&(!pwrpriv->bips_processing))
	{
		pwrpriv->bips_processing = true;
		pwrpriv->change_rfpwrstate = rf_on;
		pwrpriv->ips_leave_cnts++;
		DBG_871X("==>ips_leave cnts:%d\n",pwrpriv->ips_leave_cnts);

		if ((result = rtw_ips_pwr_up(padapter)) == _SUCCESS) {
			pwrpriv->rf_pwrstate = rf_on;
		}
		DBG_871X_LEVEL(_drv_always_, "nolinked power save leave\n");

		DBG_871X("==> ips_leave.....LED(0x%08x)...\n",rtw_read32(padapter,0x4c));
		pwrpriv->bips_processing = false;

		pwrpriv->bkeepfwalive = false;
		pwrpriv->bpower_saving = false;
	}

	return result;
}

int ips_leave(struct adapter * padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int ret;

	_enter_pwrlock(&pwrpriv->lock);
	ret = _ips_leave(padapter);
	_exit_pwrlock(&pwrpriv->lock);

	return ret;
}

#ifdef CONFIG_AUTOSUSPEND
extern void autosuspend_enter(struct adapter* padapter);
extern int autoresume_enter(struct adapter* padapter);
#endif

static bool rtw_pwr_unassociated_idle(struct adapter *adapter)
{
	struct adapter *buddy = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct xmit_priv *pxmit_priv = &adapter->xmitpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(adapter->wdinfo);
	struct cfg80211_wifidirect_info *pcfg80211_wdinfo = &adapter->cfg80211_wdinfo;
#endif

	bool ret = false;

	if (adapter_to_pwrctl(adapter)->ips_deny_time >= rtw_get_current_time()) {
		/* DBG_871X("%s ips_deny_time\n", __func__); */
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR)
		|| check_fwstate(pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS)
		|| check_fwstate(pmlmepriv, WIFI_AP_STATE)
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)
		#if defined(CONFIG_P2P) && defined(CONFIG_P2P_IPS)
		|| pcfg80211_wdinfo->is_ro_ch
		#elif defined(CONFIG_P2P)
		|| !rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
		#endif
	) {
		goto exit;
	}

	/* consider buddy, if exist */
	if (buddy) {
		struct mlme_priv *b_pmlmepriv = &(buddy->mlmepriv);
		#ifdef CONFIG_P2P
		struct wifidirect_info *b_pwdinfo = &(buddy->wdinfo);
		struct cfg80211_wifidirect_info *b_pcfg80211_wdinfo = &buddy->cfg80211_wdinfo;
		#endif

		if (check_fwstate(b_pmlmepriv, WIFI_ASOC_STATE|WIFI_SITE_MONITOR)
			|| check_fwstate(b_pmlmepriv, WIFI_UNDER_LINKING|WIFI_UNDER_WPS)
			|| check_fwstate(b_pmlmepriv, WIFI_AP_STATE)
			|| check_fwstate(b_pmlmepriv, WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE)
			#if defined(CONFIG_P2P) && defined(CONFIG_P2P_IPS)
			|| b_pcfg80211_wdinfo->is_ro_ch
			#elif defined(CONFIG_P2P)
			|| !rtw_p2p_chk_state(b_pwdinfo, P2P_STATE_NONE)
			#endif
		) {
			goto exit;
		}
	}

#if (MP_DRIVER == 1)
	if (adapter->registrypriv.mp_mode == 1)
		goto exit;
#endif

#ifdef CONFIG_INTEL_PROXIM
	if (adapter->proximity.proxim_on==true){
		return;
	}
#endif

	if (pxmit_priv->free_xmitbuf_cnt != NR_XMITBUFF ||
		pxmit_priv->free_xmit_extbuf_cnt != NR_XMIT_EXTBUFF) {
		DBG_871X_LEVEL(_drv_always_, "There are some pkts to transmit\n");
		DBG_871X_LEVEL(_drv_info_, "free_xmitbuf_cnt: %d, free_xmit_extbuf_cnt: %d\n",
			pxmit_priv->free_xmitbuf_cnt, pxmit_priv->free_xmit_extbuf_cnt);
		goto exit;
	}

	ret = true;

exit:
	return ret;
}

void rtw_ps_processor(struct adapter*padapter)
{
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
#endif /* CONFIG_P2P */
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	rt_rf_power_state rfpwrstate;

	pwrpriv->ps_processing = true;

	if (pwrpriv->bips_processing == true)
		goto exit;

	/* DBG_871X("==> fw report state(0x%x)\n",rtw_read8(padapter,0x1ca)); */
	if (pwrpriv->bHWPwrPindetect)
	{
	#ifdef CONFIG_AUTOSUSPEND
		if (padapter->registrypriv.usbss_enable)
		{
			if (pwrpriv->rf_pwrstate == rf_on)
			{
				if (padapter->net_closed == true)
					pwrpriv->ps_flag = true;

				rfpwrstate = RfOnOffDetect(padapter);
				DBG_871X("@@@@- #1  %s==> rfstate:%s\n",__FUNCTION__,(rfpwrstate==rf_on)?"rf_on":"rf_off");
				if (rfpwrstate!= pwrpriv->rf_pwrstate)
				{
					if (rfpwrstate == rf_off)
					{
						pwrpriv->change_rfpwrstate = rf_off;

						pwrpriv->bkeepfwalive = true;
						pwrpriv->brfoffbyhw = true;

						autosuspend_enter(padapter);
					}
				}
			}
		}
		else
	#endif /* CONFIG_AUTOSUSPEND */
		{
			rfpwrstate = RfOnOffDetect(padapter);
			DBG_871X("@@@@- #2  %s==> rfstate:%s\n",__FUNCTION__,(rfpwrstate==rf_on)?"rf_on":"rf_off");

			if (rfpwrstate!= pwrpriv->rf_pwrstate)
			{
				if (rfpwrstate == rf_off)
				{
					pwrpriv->change_rfpwrstate = rf_off;
					pwrpriv->brfoffbyhw = true;
					padapter->bCardDisableWOHSM = true;
					rtw_hw_suspend(padapter );
				}
				else
				{
					pwrpriv->change_rfpwrstate = rf_on;
					rtw_hw_resume(padapter );
				}
				DBG_871X("current rf_pwrstate(%s)\n",(pwrpriv->rf_pwrstate == rf_off)?"rf_off":"rf_on");
			}
		}
		pwrpriv->pwr_state_check_cnts ++;
	}

	if (pwrpriv->ips_mode_req == IPS_NONE)
		goto exit;

	if (rtw_pwr_unassociated_idle(padapter) == false)
		goto exit;

	if ((pwrpriv->rf_pwrstate == rf_on) && ((pwrpriv->pwr_state_check_cnts%4)==0))
	{
		DBG_871X("==>%s .fw_state(%x)\n",__FUNCTION__,get_fwstate(pmlmepriv));
		#if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND)
		#else
		pwrpriv->change_rfpwrstate = rf_off;
		#endif
		#ifdef CONFIG_AUTOSUSPEND
		if (padapter->registrypriv.usbss_enable)
		{
			if (pwrpriv->bHWPwrPindetect)
				pwrpriv->bkeepfwalive = true;

			if (padapter->net_closed == true)
				pwrpriv->ps_flag = true;

			#if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND)
			if (true==pwrpriv->bInternalAutoSuspend) {
				DBG_871X("<==%s .pwrpriv->bInternalAutoSuspend)(%x)\n",__FUNCTION__,pwrpriv->bInternalAutoSuspend);
			} else {
				pwrpriv->change_rfpwrstate = rf_off;
				padapter->bCardDisableWOHSM = true;
				DBG_871X("<==%s .pwrpriv->bInternalAutoSuspend)(%x) call autosuspend_enter\n",__FUNCTION__,pwrpriv->bInternalAutoSuspend);
				autosuspend_enter(padapter);
			}
			#else
			padapter->bCardDisableWOHSM = true;
			autosuspend_enter(padapter);
			#endif	/* if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */
		}
		else if (pwrpriv->bHWPwrPindetect)
		{
		}
		else
		#endif /* CONFIG_AUTOSUSPEND */
		{
			#if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND)
			pwrpriv->change_rfpwrstate = rf_off;
			#endif	/* defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */

			ips_enter(padapter);
		}
	}
exit:
	rtw_set_pwr_state_check_timer(pwrpriv);
	pwrpriv->ps_processing = false;
	return;
}

static void pwr_state_check_handler(void *FunctionContext)
{
	struct adapter *padapter = (struct adapter *)FunctionContext;
	rtw_ps_cmd(padapter);
}

/*
 *
 * Parameters
 *	padapter
 *	pslv			power state level, only could be PS_STATE_S0 ~ PS_STATE_S4
 *
 */
void rtw_set_rpwm(struct adapter *padapter, u8 pslv)
{
	u8	rpwm;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	u8 cpwm_orig = 0;
	u8 cpwm_now = 0;
	u32 cpwm_polling_start_time = 0;
	u8 pollingRes = _FAIL;
#endif

;

	pslv = PS_STATE(pslv);


	if (true == pwrpriv->btcoex_rfon)
	{
		if (pslv < PS_STATE_S4)
			pslv = PS_STATE_S3;
	}

	if ((pwrpriv->rpwm == pslv)) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,
			("%s: Already set rpwm[0x%02X], new=0x%02X!\n", __FUNCTION__, pwrpriv->rpwm, pslv));
		return;
	}

	if ((padapter->bSurpriseRemoved == true) ||
		(padapter->hw_init_completed == false))
	{
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
				 ("%s: SurpriseRemoved(%d) hw_init_completed(%d)\n",
				  __FUNCTION__, padapter->bSurpriseRemoved, padapter->hw_init_completed));

		pwrpriv->cpwm = PS_STATE_S4;

		return;
	}

	if (padapter->bDriverStopped == true)
	{
		RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
				 ("%s: change power state(0x%02X) when DriverStopped\n", __FUNCTION__, pslv));

		if (pslv < PS_STATE_S2) {
			RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_err_,
					 ("%s: Reject to enter PS_STATE(0x%02X) lower than S2 when DriverStopped!!\n", __FUNCTION__, pslv));
			return;
		}
	}

	rpwm = pslv | pwrpriv->tog;
	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("rtw_set_rpwm: rpwm=0x%02x cpwm=0x%02x\n", rpwm, pwrpriv->cpwm));

	pwrpriv->rpwm = pslv;

#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	if (rpwm & PS_ACK)
	{
		/* cpwm_orig = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HCPWM1); */
		rtw_hal_get_hwreg(padapter, HW_VAR_GET_CPWM, (u8 *)(&cpwm_orig));
	}
#endif

	rtw_hal_set_hwreg(padapter, HW_VAR_SET_RPWM, (u8 *)(&rpwm));

	pwrpriv->tog += 0x80;

	pwrpriv->cpwm = pslv;

#ifdef CONFIG_DETECT_CPWM_BY_POLLING
	if (rpwm & PS_ACK)
	{
		cpwm_polling_start_time = rtw_get_current_time();

		/* polling cpwm */
		do{
			rtw_mdelay_os(1);

			/* cpwm_now = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HCPWM1); */
			rtw_hal_get_hwreg(padapter, HW_VAR_GET_CPWM, (u8 *)(&cpwm_now));
			if ((cpwm_orig ^ cpwm_now) & 0x80) {
				pollingRes = _SUCCESS;
				break;
			}
		}while (rtw_get_passing_time_ms(cpwm_polling_start_time) < LPS_RPWM_WAIT_MS);

		if (pollingRes == _FAIL)
			DBG_871X("%s polling cpwm timeout!!!!!!!!!!\n", __FUNCTION__);
	}
#endif

;
}

u8 PS_RDY_CHECK(struct adapter * padapter);
u8 PS_RDY_CHECK(struct adapter * padapter)
{
	u32 curr_time, delta_time;
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	if (true == pwrpriv->bInSuspend )
		return false;

	curr_time = rtw_get_current_time();
	delta_time = curr_time -pwrpriv->DelayLPSLastTimeStamp;

	if (delta_time < LPS_DELAY_TIME)
	{
		return false;
	}

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) ||
		(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) ||
		(check_fwstate(pmlmepriv, WIFI_UNDER_WPS) == true) ||
		(check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) )
		return false;

	if ( (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) && (padapter->securitypriv.binstallGrpkey == false) )
	{
		DBG_871X("Group handshake still in progress !!!\n");
		return false;
	}

	if (!rtw_cfg80211_pwr_mgmt(padapter))
		return false;
	return true;
}

void rtw_set_ps_mode(struct adapter *padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
#endif /* CONFIG_P2P */

	RT_TRACE(_module_rtl871x_pwrctrl_c_, _drv_notice_,
			 ("%s: PowerMode=%d Smart_PS=%d\n",
			  __FUNCTION__, ps_mode, smart_ps));

	if (ps_mode > PM_Card_Disable) {
		RT_TRACE(_module_rtl871x_pwrctrl_c_,_drv_err_,("ps_mode:%d error\n", ps_mode));
		return;
	}

	if (pwrpriv->pwr_mode == ps_mode)
	{
		if (PS_MODE_ACTIVE == ps_mode) return;

		if ((pwrpriv->smart_ps == smart_ps) &&
			(pwrpriv->bcn_ant_mode == bcn_ant_mode))
		{
			return;
		}
	}

	/* if (pwrpriv->pwr_mode == PS_MODE_ACTIVE) */
	if (ps_mode == PS_MODE_ACTIVE)
	{
#ifdef CONFIG_P2P
		if (pwdinfo->opp_ps == 0)
#endif /* CONFIG_P2P */
		{
			DBG_871X("rtw_set_ps_mode: Leave 802.11 power save\n");
			pwrpriv->pwr_mode = ps_mode;
			rtw_set_rpwm(padapter, PS_STATE_S4);
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));
			pwrpriv->bFwCurrentInPSMode = false;
		}
	}
	else
	{
		if (PS_RDY_CHECK(padapter)
#ifdef CONFIG_BT_COEXIST
			|| (BT_1Ant(padapter) == true)
#endif
			)
		{
			DBG_871X("%s: Enter 802.11 power save\n", __FUNCTION__);
			pwrpriv->bFwCurrentInPSMode = true;
			pwrpriv->pwr_mode = ps_mode;
			pwrpriv->smart_ps = smart_ps;
			pwrpriv->bcn_ant_mode = bcn_ant_mode;
			rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_PWRMODE, (u8 *)(&ps_mode));

#ifdef CONFIG_P2P
			/*  Set CTWindow after LPS */
			if (pwdinfo->opp_ps == 1)
				p2p_ps_wk_cmd(padapter, P2P_PS_ENABLE, 0);
#endif /* CONFIG_P2P */

			rtw_set_rpwm(padapter, PS_STATE_S2);
		}
	}
}

/*
 * Return:
 *	0:	Leave OK
 *	-1:	Timeout
 *	-2:	Other error
 */
s32 LPS_RF_ON_check(struct adapter *padapter, u32 delay_ms)
{
	u32 start_time;
	u8 bAwake = false;
	s32 err = 0;


	start_time = rtw_get_current_time();
	while (1)
	{
		rtw_hal_get_hwreg(padapter, HW_VAR_FWLPS_RF_ON, &bAwake);
		if (true == bAwake)
			break;

		if (true == padapter->bSurpriseRemoved)
		{
			err = -2;
			DBG_871X("%s: device surprise removed!!\n", __FUNCTION__);
			break;
		}

		if (rtw_get_passing_time_ms(start_time) > delay_ms)
		{
			err = -1;
			DBG_871X("%s: Wait for FW LPS leave more than %u ms!!!\n", __FUNCTION__, delay_ms);
			break;
		}
		rtw_usleep_os(100);
	}

	return err;
}

/*  */
/* 	Description: */
/* 		Enter the leisure power save mode. */
/*  */
void LPS_Enter(struct adapter *padapter)
{
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct adapter *buddy = padapter->pbuddy_adapter;

	if (PS_RDY_CHECK(padapter) == false)
		return;

	if (pwrpriv->bLeisurePs) {
		/*  Idle for a while if we connect to AP a while ago. */
		if (pwrpriv->LpsIdleCount >= 2) /*   4 Sec */
		{
			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
			{
				pwrpriv->bpower_saving = true;
				DBG_871X("%s smart_ps:%d\n", __func__, pwrpriv->smart_ps);
				/* For Tenda W311R IOT issue */
				rtw_set_ps_mode(padapter, pwrpriv->power_mgnt, pwrpriv->smart_ps, 0x40);
			}
		}
		else
			pwrpriv->LpsIdleCount++;
	}
}

/*  */
/* 	Description: */
/* 		Leave the leisure power save mode. */
/*  */
void LPS_Leave(struct adapter *padapter)
{
#define LPS_LEAVE_TIMEOUT_MS 100

	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(padapter);
	u32 start_time;
	u8 bAwake = false;

	if (pwrpriv->bLeisurePs) {
		if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
			rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0x40);

			if (pwrpriv->pwr_mode == PS_MODE_ACTIVE)
				LPS_RF_ON_check(padapter, LPS_LEAVE_TIMEOUT_MS);
		}
	}

	pwrpriv->bpower_saving = false;
}

/*  */
/*  Description: Leave all power save mode: LPS, FwLPS, IPS if needed. */
/*  Move code to function by tynli. 2010.03.26. */
/*  */
void LeaveAllPowerSaveMode(struct adapter *Adapter)
{
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	u8	enqueue = 0;

;

	/* DBG_871X("%s.....\n",__FUNCTION__); */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
	{ /* connect */
#ifdef CONFIG_P2P
		p2p_ps_wk_cmd(Adapter, P2P_PS_DISABLE, enqueue);
#endif /* CONFIG_P2P */

		rtw_lps_ctrl_wk_cmd(Adapter, LPS_CTRL_LEAVE, enqueue);
	} else {
		if (adapter_to_pwrctl(Adapter)->rf_pwrstate== rf_off)
		{
			#ifdef CONFIG_AUTOSUSPEND
			if (Adapter->registrypriv.usbss_enable)
			{
				#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
				usb_disable_autosuspend(adapter_to_dvobj(Adapter)->pusbdev);
				#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
				adapter_to_dvobj(Adapter)->pusbdev->autosuspend_disabled = Adapter->bDisableAutosuspend;/* autosuspend disabled by the user */
				#endif
			}
			#endif
		}
	}

;
}

void rtw_init_pwrctrl_priv(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	_init_pwrlock(&pwrctrlpriv->lock);
	pwrctrlpriv->rf_pwrstate = rf_on;
	pwrctrlpriv->ips_enter_cnts=0;
	pwrctrlpriv->ips_leave_cnts=0;
	pwrctrlpriv->bips_processing = false;

	pwrctrlpriv->ips_mode = padapter->registrypriv.ips_mode;
	pwrctrlpriv->ips_mode_req = padapter->registrypriv.ips_mode;

	pwrctrlpriv->pwr_state_check_interval = RTW_PWR_STATE_CHK_INTERVAL;
	pwrctrlpriv->pwr_state_check_cnts = 0;
	pwrctrlpriv->bInternalAutoSuspend = false;
	pwrctrlpriv->bInSuspend = false;
	pwrctrlpriv->bkeepfwalive = false;

#ifdef CONFIG_AUTOSUSPEND
	pwrctrlpriv->pwr_state_check_interval = (pwrctrlpriv->bHWPwrPindetect) ?1000:2000;
#endif

	pwrctrlpriv->LpsIdleCount = 0;
	if (padapter->registrypriv.mp_mode == 1)
		pwrctrlpriv->power_mgnt =PS_MODE_ACTIVE ;
	else
		pwrctrlpriv->power_mgnt =padapter->registrypriv.power_mgnt;/*  PS_MODE_MIN; */
	pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt)?true:false;

	pwrctrlpriv->bFwCurrentInPSMode = false;

	pwrctrlpriv->rpwm = 0;
	pwrctrlpriv->cpwm = PS_STATE_S4;

	pwrctrlpriv->pwr_mode = PS_MODE_ACTIVE;
	pwrctrlpriv->smart_ps = padapter->registrypriv.smart_ps;
	pwrctrlpriv->bcn_ant_mode = 0;

	pwrctrlpriv->tog = 0x80;

	pwrctrlpriv->btcoex_rfon = false;

	_init_timer(&(pwrctrlpriv->pwr_state_check_timer), padapter->pnetdev, pwr_state_check_handler, (u8 *)padapter);

	#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	pwrctrlpriv->early_suspend.suspend = NULL;
	rtw_register_early_suspend(pwrctrlpriv);
	#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */


;

}


void rtw_free_pwrctrl_priv(struct adapter *adapter)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(adapter);

	#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctrlpriv);
	#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

	_free_pwrlock(&pwrctrlpriv->lock);
}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
inline bool rtw_is_earlysuspend_registered(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->early_suspend.suspend) ? true : false;
}

inline bool rtw_is_do_late_resume(struct pwrctrl_priv *pwrpriv)
{
	return (pwrpriv->do_late_resume) ? true : false;
}

inline void rtw_set_do_late_resume(struct pwrctrl_priv *pwrpriv, bool enable)
{
	pwrpriv->do_late_resume = enable;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
extern int rtw_resume_process(struct adapter *padapter);
static void rtw_early_suspend(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_871X("%s\n",__FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, false);
}

static void rtw_late_resume(struct early_suspend *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	struct adapter *adapter = dvobj->if1;

	DBG_871X("%s\n",__FUNCTION__);
	if (pwrpriv->do_late_resume) {
		rtw_set_do_late_resume(pwrpriv, false);
		rtw_resume_process(adapter);
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	/* jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit */
	pwrpriv->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	register_early_suspend(&pwrpriv->early_suspend);
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, false);

	if (pwrpriv->early_suspend.suspend)
		unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_ANDROID_POWER
extern int rtw_resume_process(struct adapter *padapter);
static void rtw_early_suspend(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	DBG_871X("%s\n",__FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, false);
}

static void rtw_late_resume(android_early_suspend_t *h)
{
	struct pwrctrl_priv *pwrpriv = container_of(h, struct pwrctrl_priv, early_suspend);
	struct dvobj_priv *dvobj = pwrctl_to_dvobj(pwrpriv);
	struct adapter *adapter = dvobj->if1;

	DBG_871X("%s\n",__FUNCTION__);
	if (pwrpriv->do_late_resume) {
		rtw_set_do_late_resume(pwrpriv, false);
		rtw_resume_process(adapter);
	}
}

void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	/* jeff: set the early suspend level before blank screen, so we wll do late resume after scree is lit */
	pwrpriv->early_suspend.level = ANDROID_EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 20;
	pwrpriv->early_suspend.suspend = rtw_early_suspend;
	pwrpriv->early_suspend.resume = rtw_late_resume;
	android_register_early_suspend(&pwrpriv->early_suspend);
}

void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv)
{
	DBG_871X("%s\n", __FUNCTION__);

	rtw_set_do_late_resume(pwrpriv, false);

	if (pwrpriv->early_suspend.suspend)
		android_unregister_early_suspend(&pwrpriv->early_suspend);

	pwrpriv->early_suspend.suspend = NULL;
	pwrpriv->early_suspend.resume = NULL;
}
#endif /* CONFIG_ANDROID_POWER */

u8 rtw_interface_ps_func(struct adapter *padapter, enum HAL_INTF_PS_FUNC efunc_id,u8* val)
{
	u8 bResult = true;
	rtw_hal_intf_ps_func(padapter,efunc_id,val);

	return bResult;
}


inline void rtw_set_ips_deny(struct adapter *padapter, u32 ms)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ms);
}

/*
* rtw_pwr_wakeup - Wake the NIC up from: 1)IPS. 2)USB autosuspend
* @adapter: pointer to struct adapter structure
* @ips_deffer_ms: the ms wiil prevent from falling into IPS after wakeup
* Return _SUCCESS or _FAIL
*/

int _rtw_pwr_wakeup(struct adapter *padapter, u32 ips_deffer_ms, const char *caller)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = _SUCCESS;
	u32 start = rtw_get_current_time();

	if (pwrpriv->ips_deny_time < rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);


	if (pwrpriv->ps_processing) {
		DBG_871X("%s wait ps_processing...\n", __func__);
		while (pwrpriv->ps_processing && rtw_get_passing_time_ms(start) <= 3000)
			rtw_msleep_os(10);
		if (pwrpriv->ps_processing)
			DBG_871X("%s wait ps_processing timeout\n", __func__);
		else
			DBG_871X("%s wait ps_processing done\n", __func__);
	}

	if (rtw_hal_sreset_inprogress(padapter)) {
		DBG_871X("%s wait sreset_inprogress...\n", __func__);
		while (rtw_hal_sreset_inprogress(padapter) && rtw_get_passing_time_ms(start) <= 4000)
			rtw_msleep_os(10);
		if (rtw_hal_sreset_inprogress(padapter))
			DBG_871X("%s wait sreset_inprogress timeout\n", __func__);
		else
			DBG_871X("%s wait sreset_inprogress done\n", __func__);
	}
	if (pwrpriv->bInternalAutoSuspend == false && pwrpriv->bInSuspend) {
		DBG_871X("%s wait bInSuspend...\n", __func__);
		while (pwrpriv->bInSuspend
			&& ((rtw_get_passing_time_ms(start) <= 3000 && !rtw_is_do_late_resume(pwrpriv))
				|| (rtw_get_passing_time_ms(start) <= 500 && rtw_is_do_late_resume(pwrpriv)))
		) {
			rtw_msleep_os(10);
		}
		if (pwrpriv->bInSuspend)
			DBG_871X("%s wait bInSuspend timeout\n", __func__);
		else
			DBG_871X("%s wait bInSuspend done\n", __func__);
	}

	/* System suspend is not allowed to wakeup */
	if ((pwrpriv->bInternalAutoSuspend == false) && (true == pwrpriv->bInSuspend )){
		ret = _FAIL;
		goto exit;
	}

	/* block??? */
	if ((pwrpriv->bInternalAutoSuspend == true)  && (padapter->net_closed == true)) {
		ret = _FAIL;
		goto exit;
	}

	/* I think this should be check in IPS, LPS, autosuspend functions... */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
	{
#if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND)
		if (true==pwrpriv->bInternalAutoSuspend){
			if (0==pwrpriv->autopm_cnt){
			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
				if (usb_autopm_get_interface(adapter_to_dvobj(padapter)->pusbintf) < 0)
				{
					DBG_871X( "can't get autopm:\n");
				}
			#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))
				usb_autopm_disable(adapter_to_dvobj(padapter)->pusbintf);
			#else
				usb_autoresume_device(adapter_to_dvobj(padapter)->pusbdev, 1);
			#endif
			pwrpriv->autopm_cnt++;
			}
#endif	/* if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */
		ret = _SUCCESS;
		goto exit;
#if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND)
		}
#endif	/* if defined (CONFIG_BT_COEXIST)&& defined (CONFIG_AUTOSUSPEND) */
	}

	if (rf_off == pwrpriv->rf_pwrstate )
	{
#ifdef CONFIG_AUTOSUSPEND
		 if (pwrpriv->brfoffbyhw==true)
		{
			DBG_8192C("hw still in rf_off state ...........\n");
			ret = _FAIL;
			goto exit;
		}
		else if (padapter->registrypriv.usbss_enable)
		{
			DBG_8192C("%s call autoresume_enter....\n",__FUNCTION__);
			if (_FAIL ==  autoresume_enter(padapter))
			{
				DBG_8192C("======> autoresume fail.............\n");
				ret = _FAIL;
				goto exit;
			}
		}
		else
#endif
		{
			DBG_8192C("%s call ips_leave....\n",__FUNCTION__);
			if (_FAIL ==  ips_leave(padapter))
			{
				DBG_8192C("======> ips_leave fail.............\n");
				ret = _FAIL;
				goto exit;
			}
		}
	}

	/* TODO: the following checking need to be merged... */
	if (padapter->bDriverStopped
		|| !padapter->bup
		|| !padapter->hw_init_completed
	){
		DBG_8192C("%s: bDriverStopped=%d, bup=%d, hw_init_completed=%u\n"
			, caller
			, padapter->bDriverStopped
			, padapter->bup
			, padapter->hw_init_completed);
		ret= false;
		goto exit;
	}

exit:
	if (pwrpriv->ips_deny_time < rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms))
		pwrpriv->ips_deny_time = rtw_get_current_time() + rtw_ms_to_systime(ips_deffer_ms);
	return ret;

}

int rtw_pm_set_lps(struct adapter *padapter, u8 mode)
{
	int	ret = 0;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if ( mode < PS_MODE_NUM )
	{
		if (pwrctrlpriv->power_mgnt !=mode)
		{
			if (PS_MODE_ACTIVE == mode)
			{
				LeaveAllPowerSaveMode(padapter);
			}
			else
			{
				pwrctrlpriv->LpsIdleCount = 2;
			}
			pwrctrlpriv->power_mgnt = mode;
			pwrctrlpriv->bLeisurePs = (PS_MODE_ACTIVE != pwrctrlpriv->power_mgnt)?true:false;
		}
	}
	else
	{
		ret = -EINVAL;
	}

	return ret;
}

int rtw_pm_set_ips(struct adapter *padapter, u8 mode)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if ( mode == IPS_NORMAL || mode == IPS_LEVEL_2 ) {
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_871X("%s %s\n", __FUNCTION__, mode == IPS_NORMAL?"IPS_NORMAL":"IPS_LEVEL_2");
		return 0;
	}
	else if (mode ==IPS_NONE){
		rtw_ips_mode_req(pwrctrlpriv, mode);
		DBG_871X("%s %s\n", __FUNCTION__, "IPS_NONE");
		if ((padapter->bSurpriseRemoved ==0)&&(_FAIL == rtw_pwr_wakeup(padapter)) )
			return -EFAULT;
	}
	else {
		return -EINVAL;
	}
	return 0;
}
